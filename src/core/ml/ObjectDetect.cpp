#include "ObjectDetect.h"

#include "ModelRegistry.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace Ml
{
   namespace ObjectDetect
   {
      namespace
      {
         constexpr int kSide = 640;

         const char* const kCoco80[80] = {
            "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
            "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
            "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
            "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
            "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
            "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
            "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
            "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
            "hair drier", "toothbrush"
         };

         struct CacheState
         {
            std::mutex mu;
            uint64_t key = 0;
            std::vector<Box> boxes;
            std::string lastError;
            LatestWorker worker;
         };

         CacheState& C()
         {
            static CacheState* c = new CacheState();
            return *c;
         }

         float Sigmoid(float x)
         {
            x = std::clamp(x, -20.0f, 20.0f);
            return 1.0f / (1.0f + std::exp(-x));
         }
      } // namespace

      const char* CocoLabel(int classId)
      {
         return (classId >= 0 && classId < 80) ? kCoco80[classId] : "object";
      }

      bool Available() { return ModelRegistry::Available("dfine_s"); }
      std::string Missing() { return ModelRegistry::Missing("dfine_s"); }

      bool DetectNow(const std::vector<uint8_t>& rgba, int w, int h,
                     std::vector<Box>& outBoxes, std::string& err,
                     float scoreThreshold)
      {
         outBoxes.clear();
         if (w < 2 || h < 2 || rgba.size() < static_cast<size_t>(w) * h * 4)
         {
            err = "empty picture";
            return false;
         }

         const size_t plane = static_cast<size_t>(kSide) * kSide;
         std::vector<float> chw(plane * 3, 0.0f);
         for (int y = 0; y < kSide; y++)
         {
            const float sy = std::clamp((y + 0.5f) * h / kSide - 0.5f, 0.0f, static_cast<float>(h - 1));
            const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, h - 1);
            const float fy = sy - y0;
            for (int x = 0; x < kSide; x++)
            {
               const float sx = std::clamp((x + 0.5f) * w / kSide - 0.5f, 0.0f, static_cast<float>(w - 1));
               const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, w - 1);
               const float fx = sx - x0;
               const uint8_t* a = &rgba[(static_cast<size_t>(y0) * w + x0) * 4];
               const uint8_t* b = &rgba[(static_cast<size_t>(y0) * w + x1) * 4];
               const uint8_t* c = &rgba[(static_cast<size_t>(y1) * w + x0) * 4];
               const uint8_t* d = &rgba[(static_cast<size_t>(y1) * w + x1) * 4];
               for (int k = 0; k < 3; k++)
               {
                  const float top = a[k] + (b[k] - a[k]) * fx;
                  const float bot = c[k] + (d[k] - c[k]) * fx;
                  chw[k * plane + static_cast<size_t>(y) * kSide + x] = (top + (bot - top) * fy) / 255.0f;
               }
            }
         }

         std::vector<Tracking::OrtRuntime::Tensor> outs;
         const bool ok = ModelRegistry::WithModel("dfine_s", [&](Tracking::OrtRuntime& rt, Tracking::OrtModel* m, std::string& e) {
            Tracking::OrtRuntime::Input in;
            in.name = "pixel_values";
            in.shape = { 1, 3, kSide, kSide };
            in.f32 = chw.data();
            return rt.RunNamed(m, { in }, outs, e);
         }, err);
         if (!ok)
            return false;
         if (outs.size() < 2 || outs[0].shape.size() != 3 || outs[1].shape.size() != 3)
         {
            err = "unexpected D-FINE-S output";
            return false;
         }

         const int numQ = static_cast<int>(outs[0].shape[1]);
         const int numC = static_cast<int>(outs[0].shape[2]);
         const float* logits = outs[0].data.data();
         const float* boxes = outs[1].data.data();

         for (int q = 0; q < numQ; q++)
         {
            const float* row = logits + static_cast<size_t>(q) * numC;
            int bestC = 0;
            float bestLogit = row[0];
            for (int c = 1; c < numC; c++)
            {
               if (row[c] > bestLogit) { bestLogit = row[c]; bestC = c; }
            }
            const float sc = Sigmoid(bestLogit);
            if (sc < scoreThreshold)
               continue;
            const float cx = boxes[q * 4 + 0];
            const float cy = boxes[q * 4 + 1];
            const float bw = boxes[q * 4 + 2];
            const float bh = boxes[q * 4 + 3];
            Box bx;
            bx.x0 = std::clamp((cx - bw * 0.5f) * w, 0.0f, static_cast<float>(w));
            bx.y0 = std::clamp((cy - bh * 0.5f) * h, 0.0f, static_cast<float>(h));
            bx.x1 = std::clamp((cx + bw * 0.5f) * w, 0.0f, static_cast<float>(w));
            bx.y1 = std::clamp((cy + bh * 0.5f) * h, 0.0f, static_cast<float>(h));
            if (bx.x1 - bx.x0 < 4.0f || bx.y1 - bx.y0 < 4.0f)
               continue;
            bx.score = sc;
            bx.classId = bestC;
            bx.label = CocoLabel(bestC);
            outBoxes.push_back(std::move(bx));
         }

         std::sort(outBoxes.begin(), outBoxes.end(), [](const Box& a, const Box& b) { return a.score > b.score; });
         return true;
      }

      void Request(std::shared_ptr<const Picture> pic, uint64_t key)
      {
         if (!pic || !Available())
            return;
         CacheState& c = C();
         {
            std::lock_guard<std::mutex> lk(c.mu);
            if (c.key == key && !c.boxes.empty())
               return;
         }
         c.worker.Submit([pic = std::move(pic), key](uint64_t) {
            std::vector<Box> boxes;
            std::string err;
            const bool ok = DetectNow(pic->rgba, pic->w, pic->h, boxes, err);
            CacheState& st = C();
            std::lock_guard<std::mutex> lk(st.mu);
            st.lastError = ok ? std::string() : err;
            if (ok)
            {
               st.boxes.swap(boxes);
               st.key = key;
            }
         });
      }

      bool Ready(uint64_t key)
      {
         std::lock_guard<std::mutex> lk(C().mu);
         return C().key == key;
      }

      bool Busy() { return C().worker.Busy(); }

      bool GetBoxes(uint64_t key, std::vector<Box>& outBoxes)
      {
         std::lock_guard<std::mutex> lk(C().mu);
         if (C().key != key)
            return false;
         outBoxes = C().boxes;
         return true;
      }

      std::string LastError()
      {
         std::lock_guard<std::mutex> lk(C().mu);
         return C().lastError;
      }

      void ClearCache()
      {
         std::lock_guard<std::mutex> lk(C().mu);
         C().boxes.clear();
         C().key = 0;
      }
   } // namespace ObjectDetect
} // namespace Ml
