#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "GLUtil.h"

// Blender-standard procedural texture set: Voronoi, Brick, Magic, Wave, and
// Musgrave, each with its own dedicated parameter block (unlike NoiseNode's
// single shared param set, these patterns don't share enough shape to make
// one param list clean). One shader, branched on textureType, keeps this to
// a single node class/registration the same way NoiseNode covers six modes.
class TextureNode : public INode
{
public:
   static INode* Create() { return new TextureNode(); }
   static const std::vector<std::string>& TypeNames();
   static const std::vector<std::string>& VoronoiDistanceNames();
   static const std::vector<std::string>& VoronoiFeatureNames();
   static const std::vector<std::string>& WaveTypeNames();
   static const std::vector<std::string>& WaveProfileNames();
   static const std::vector<std::string>& WaveBandsDirectionNames();
   static const std::vector<std::string>& MusgraveTypeNames();

   ~TextureNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   int textureType = 0; // Voronoi / Brick / Magic / Wave / Musgrave
   float width = 1024.0f;
   float height = 1024.0f;
   float scale = 6.0f;
   float seed = 0.0f;
   float contrast = 1.0f;
   float brightness = 0.0f;
   float lowColor[3] = { 0.0f, 0.0f, 0.0f };
   float highColor[3] = { 1.0f, 1.0f, 1.0f };

   // Voronoi
   int voronoiDistance = 0;
   int voronoiFeature = 0;
   float voronoiRandomness = 1.0f;
   float voronoiMinkowskiExponent = 0.5f;
   float voronoiSmoothness = 0.2f;
   bool voronoiCellColor = false;

   // Brick
   float brickWidth = 0.5f;
   float brickHeight = 0.25f;
   float brickRowOffset = 0.5f;
   float brickMortarSize = 0.02f;
   float brickMortarSmooth = 0.1f;
   float brickBias = 0.0f;
   float mortarColor[3] = { 0.05f, 0.05f, 0.05f };

   // Magic
   float magicDepth = 2.0f;
   float magicDistortion = 1.5f;

   // Wave
   int waveType = 0; // Bands / Rings
   int waveProfile = 0; // Sine / Saw / Triangle
   int waveBandsDirection = 0; // X / Y / Diagonal
   float waveDistortion = 0.0f;
   float waveDetail = 2.0f;
   float waveDetailScale = 1.0f;
   float wavePhaseOffset = 0.0f;

   // Musgrave
   int musgraveType = 0; // fBm / Multifractal / Hybrid Multifractal / Ridged Multifractal / Hetero Terrain
   float musgraveDimension = 2.0f;
   float musgraveLacunarity = 2.0f;
   float musgraveOctaves = 4.0f;
   float musgraveGain = 1.0f;
   float musgraveOffset = 1.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("textureType", textureType);
      v.Float("width", width); v.Float("height", height);
      v.Float("scale", scale); v.Float("seed", seed);
      v.Float("contrast", contrast); v.Float("brightness", brightness);
      v.Color("lowColor", lowColor); v.Color("highColor", highColor);

      v.Int("voronoiDistance", voronoiDistance);
      v.Int("voronoiFeature", voronoiFeature);
      v.Float("voronoiRandomness", voronoiRandomness);
      v.Float("voronoiMinkowskiExponent", voronoiMinkowskiExponent);
      v.Float("voronoiSmoothness", voronoiSmoothness);
      v.Bool("voronoiCellColor", voronoiCellColor);

      v.Float("brickWidth", brickWidth);
      v.Float("brickHeight", brickHeight);
      v.Float("brickRowOffset", brickRowOffset);
      v.Float("brickMortarSize", brickMortarSize);
      v.Float("brickMortarSmooth", brickMortarSmooth);
      v.Float("brickBias", brickBias);
      v.Color("mortarColor", mortarColor);

      v.Float("magicDepth", magicDepth);
      v.Float("magicDistortion", magicDistortion);

      v.Int("waveType", waveType);
      v.Int("waveProfile", waveProfile);
      v.Int("waveBandsDirection", waveBandsDirection);
      v.Float("waveDistortion", waveDistortion);
      v.Float("waveDetail", waveDetail);
      v.Float("waveDetailScale", waveDetailScale);
      v.Float("wavePhaseOffset", wavePhaseOffset);

      v.Int("musgraveType", musgraveType);
      v.Float("musgraveDimension", musgraveDimension);
      v.Float("musgraveLacunarity", musgraveLacunarity);
      v.Float("musgraveOctaves", musgraveOctaves);
      v.Float("musgraveGain", musgraveGain);
      v.Float("musgraveOffset", musgraveOffset);
   }

private:
   bool EnsureShader();

   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};
