#include "AudioFileWriter.h"


#include "shine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

namespace
{
   void WriteU32(FILE* f,uint32_t v){fwrite(&v,4,1,f);} void WriteU16(FILE* f,uint16_t v){fwrite(&v,2,1,f);}
   int16_t FloatToPcm16(float v){return (int16_t)std::lround(std::clamp(v,-1.0f,1.0f)*32767.0f);}
   std::string Lower(std::string s){for(char&c:s)c=(char)std::tolower((unsigned char)c);return s;}
   struct FlacWriterHolder
   {
      std::unique_ptr<juce::FileOutputStream> stream;
      std::unique_ptr<juce::AudioFormatWriter> writer;
      std::vector<std::vector<float>> planar;
      std::vector<const float*> pointers;
   };
   struct Mp3WriterHolder
   {
      shine_t encoder = nullptr;
      std::vector<int16_t> pending;
   };
   std::wstring Utf8ToWide(const std::string& text)
   {
      if(text.empty())return {};
      const int n=MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,nullptr,0);
      std::wstring out((size_t)n,L'\0');
      MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,out.data(),n);
      if(!out.empty())out.pop_back();
      return out;
   }
   FILE* OpenBinaryWrite(const std::string& path){return _wfopen(Utf8ToWide(path).c_str(),L"wb");}
}

AudioFileWriter::AudioFileWriter()=default;
AudioFileWriter::~AudioFileWriter(){Close();}
bool AudioFileWriter::IsOpen()const{return mFile||mExtAudioFile||(mMp3File&&mShineHandle);}
AudioFileWriter::Format AudioFileWriter::FormatFromPath(const std::string&p){auto s=Lower(p);if(s.size()>=5&&s.substr(s.size()-5)==".flac")return Format::Flac;if(s.size()>=4&&s.substr(s.size()-4)==".mp3")return Format::Mp3;return Format::Wav;}
const char* AudioFileWriter::ExtensionForFormat(Format f){return f==Format::Flac?"flac":f==Format::Mp3?"mp3":"wav";}
int64_t AudioFileWriter::BytesWritten()const{return mFormat==Format::Wav?mFramesWritten*mNumChannels*2:mBytesWrittenDirect;}

bool AudioFileWriter::Open(const std::string&path,double rate,int channels,Format format)
{
   Close();if(rate<=0||channels<=0)return false;mPath=path;mSampleRate=rate;mNumChannels=std::max(1,channels);mFramesWritten=0;mBytesWrittenDirect=0;mFormat=format==Format::Auto?FormatFromPath(path):format;
   if(mFormat==Format::Flac)
   {
      auto holder=std::make_unique<FlacWriterHolder>();juce::File target(juce::String::fromUTF8(path.c_str()));target.deleteFile();holder->stream=std::make_unique<juce::FileOutputStream>(target);
      if(holder->stream->openedOk())
      {
         juce::FlacAudioFormat flac;
         auto* writer=flac.createWriterFor(holder->stream.get(),rate,(unsigned)mNumChannels,24,{},0);
         if(writer){holder->stream.release();holder->writer.reset(writer);mExtAudioFile=holder.release();return true;}
      }
      return false;
   }
   if(mFormat==Format::Mp3)
   {
      shine_config_t cfg;shine_set_config_mpeg_defaults(&cfg.mpeg);cfg.wave.samplerate=(int)std::lround(rate);cfg.wave.channels=mNumChannels==1?PCM_MONO:PCM_STEREO;cfg.mpeg.bitr=192;
      shine_t s=shine_initialize(&cfg);if(s){mMp3File=OpenBinaryWrite(path);if(!mMp3File){shine_close(s);return false;}auto*h=new Mp3WriterHolder();h->encoder=s;mShineHandle=h;return true;}return false;
   }
   mFormat=Format::Wav;mFile=OpenBinaryWrite(path);if(!mFile)return false;
   const uint32_t sr=(uint32_t)std::lround(rate);const uint16_t bits=16,align=(uint16_t)(mNumChannels*2);const uint32_t bytes=sr*align;
   fwrite("RIFF",1,4,mFile);WriteU32(mFile,0);fwrite("WAVEfmt ",1,8,mFile);WriteU32(mFile,16);WriteU16(mFile,1);WriteU16(mFile,(uint16_t)mNumChannels);WriteU32(mFile,sr);WriteU32(mFile,bytes);WriteU16(mFile,align);WriteU16(mFile,bits);fwrite("data",1,4,mFile);WriteU32(mFile,0);return true;
}

void AudioFileWriter::Append(const float*data,int frames)
{
   if(!data||frames<=0||!IsOpen())return;
   if(mFormat==Format::Flac&&mExtAudioFile)
   {
      auto*h=(FlacWriterHolder*)mExtAudioFile;h->planar.assign((size_t)mNumChannels,std::vector<float>((size_t)frames));h->pointers.resize((size_t)mNumChannels);
      for(int ch=0;ch<mNumChannels;++ch){for(int i=0;i<frames;++i)h->planar[ch][i]=data[i*mNumChannels+ch];h->pointers[ch]=h->planar[ch].data();}
      h->writer->writeFromFloatArrays(h->pointers.data(),mNumChannels,frames);mFramesWritten+=frames;return;
   }
   if(mFormat==Format::Mp3&&mShineHandle&&mMp3File)
   {
      auto*h=(Mp3WriterHolder*)mShineHandle;const int n=frames*mNumChannels;const size_t old=h->pending.size();h->pending.resize(old+(size_t)n);for(int i=0;i<n;++i)h->pending[old+(size_t)i]=FloatToPcm16(data[i]);
      const int pass=shine_samples_per_pass(h->encoder)*mNumChannels;size_t consumed=0;while(h->pending.size()-consumed>=(size_t)pass){int written=0;unsigned char*out=shine_encode_buffer_interleaved(h->encoder,h->pending.data()+consumed,&written);if(written>0&&out){fwrite(out,1,written,mMp3File);mBytesWrittenDirect+=written;}consumed+=(size_t)pass;}if(consumed>0)h->pending.erase(h->pending.begin(),h->pending.begin()+(std::ptrdiff_t)consumed);mFramesWritten+=frames;return;
   }
   if(mFile){const int n=frames*mNumChannels;static thread_local std::vector<int16_t> pcm;pcm.resize(n);for(int i=0;i<n;++i)pcm[i]=FloatToPcm16(data[i]);fwrite(pcm.data(),sizeof(int16_t),n,mFile);mFramesWritten+=frames;}
}

void AudioFileWriter::CloseFlac(){if(!mExtAudioFile)return;auto*h=(FlacWriterHolder*)mExtAudioFile;h->writer.reset();delete h;mExtAudioFile=nullptr;std::error_code ec;mBytesWrittenDirect=(int64_t)fs::file_size(fs::u8path(mPath),ec);}
void AudioFileWriter::CloseMp3(){if(mShineHandle){auto*h=(Mp3WriterHolder*)mShineHandle;const int pass=shine_samples_per_pass(h->encoder)*mNumChannels;if(!h->pending.empty()){h->pending.resize((size_t)pass,0);int written=0;auto*data=shine_encode_buffer_interleaved(h->encoder,h->pending.data(),&written);if(written>0&&data&&mMp3File)fwrite(data,1,written,mMp3File);}int written=0;auto*data=shine_flush(h->encoder,&written);if(written>0&&data&&mMp3File)fwrite(data,1,written,mMp3File);shine_close(h->encoder);delete h;mShineHandle=nullptr;}if(mMp3File){fclose(mMp3File);mMp3File=nullptr;std::error_code ec;mBytesWrittenDirect=(int64_t)fs::file_size(fs::u8path(mPath),ec);}}
void AudioFileWriter::CloseWav(){if(!mFile)return;const uint32_t bytes=(uint32_t)(mFramesWritten*mNumChannels*2);fseek(mFile,4,SEEK_SET);WriteU32(mFile,36+bytes);fseek(mFile,40,SEEK_SET);WriteU32(mFile,bytes);fclose(mFile);mFile=nullptr;}
void AudioFileWriter::Close(){CloseWav();CloseFlac();CloseMp3();}

namespace AudioRecordings
{
   std::string GetRecordingsDirectory(){const char*local=getenv("LOCALAPPDATA");fs::path dir=(local?fs::u8path(local):fs::temp_directory_path())/"Infinite"/"Recordings";std::error_code ec;fs::create_directories(dir,ec);return dir.u8string();}
   std::string GenerateFilePath(const std::string&prefix,const std::string&ext){auto now=std::chrono::system_clock::now();auto tt=std::chrono::system_clock::to_time_t(now);std::tm tm{};localtime_s(&tm,&tt);auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())%1000;static std::atomic<uint32_t>count{0};char name[256];snprintf(name,sizeof(name),"recording_%s_%04d%02d%02d_%02d%02d%02d_%03d_%u.%s",prefix.c_str(),tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec,(int)ms.count(),++count,ext.c_str());return (fs::u8path(GetRecordingsDirectory())/name).u8string();}
   bool WriteWav(const std::string&path,const float*data,int frames,double rate,int channels){if(!data||frames<=0||rate<=0)return false;AudioFileWriter w;if(!w.Open(path,rate,channels,AudioFileWriter::Format::Wav))return false;w.Append(data,frames);w.Close();return true;}
}

