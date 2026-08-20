#include "media/media_analysis.h"
#include "model/id.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ytp { namespace {
QString ffmpeg(){const auto found=QStandardPaths::findExecutable("ffmpeg");return found.isEmpty()?QStringLiteral("C:/msys64/ucrt64/bin/ffmpeg.exe"):found;}
void fail(QString* out,const QString& message){if(out)*out=message;}
double number(const QJsonValue& value){if(value.isDouble())return value.toDouble();const auto s=value.toString();if(s.contains(':')){const auto p=s.split(':');if(p.size()==3)return p[0].toDouble()*3600+p[1].toDouble()*60+p[2].toDouble();}return s.toDouble();}
}

std::vector<TranscriptWord> MediaAnalysis::parseWhisperJson(const QByteArray& json,QString* error){
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(json,&parse);if(parse.error!=QJsonParseError::NoError){fail(error,parse.errorString());return{};}
    QJsonArray values;if(document.isArray())values=document.array();else {const auto root=document.object();values=root.value("transcription").toArray();if(values.isEmpty())values=root.value("segments").toArray();}
    std::vector<TranscriptWord> result;
    for(const auto& value:values){const auto object=value.toObject();const auto words=object.value("words").toArray();
        if(!words.isEmpty()){for(const auto& wv:words){const auto w=wv.toObject();const double start=number(w.value("start"));double end=number(w.value("end"));if(end<=start)end=start+number(w.value("duration"));const auto text=w.value("word").toString(w.value("text").toString()).trimmed();if(!text.isEmpty()&&end>start)result.push_back({.id=createId(),.start=Rational{static_cast<std::int64_t>(std::llround(start*1000)),1000},.duration=Rational{static_cast<std::int64_t>(std::llround((end-start)*1000)),1000},.text=text.toStdString(),.confidence=w.value("probability").toDouble(w.value("confidence").toDouble(1))});}}
        else {double start=number(object.value("start"));double end=number(object.value("end"));const auto offsets=object.value("offsets").toObject();const auto timestamps=object.value("timestamps").toObject();if(end<=start&&!offsets.isEmpty()){start=number(offsets.value("from"))/1000.0;end=number(offsets.value("to"))/1000.0;}if(end<=start&&!timestamps.isEmpty()){start=number(timestamps.value("from"));end=number(timestamps.value("to"));}if(end<=start)end=start+number(object.value("duration"));const auto text=object.value("text").toString().trimmed();if(!text.isEmpty()&&end>start)result.push_back({.id=createId(),.start=Rational{static_cast<std::int64_t>(std::llround(start*1000)),1000},.duration=Rational{static_cast<std::int64_t>(std::llround((end-start)*1000)),1000},.text=text.toStdString(),.confidence=object.value("confidence").toDouble(1)});}
    }
    std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.start<b.start;});if(result.empty())fail(error,"Whisper returned no timed transcript entries.");return result;
}

std::vector<TranscriptWord> MediaAnalysis::transcribe(const QString& mediaPath,const QString& modelPath,const QString& language,QString* error){
    if(modelPath.isEmpty()){fail(error,"Choose a local whisper.cpp model (.bin) first.");return{};}QTemporaryDir temp;if(!temp.isValid()){fail(error,"Could not create transcription workspace.");return{};}const auto output=temp.filePath("transcript.json");
    auto escaped=[](QString value){value.replace('\\',"/");value.replace(":","\\:");value.replace("'","\\'");return value;};
    const auto filter=QString("whisper=model='%1':language=%2:destination='%3':format=json:use_gpu=false").arg(escaped(modelPath),language.isEmpty()?"auto":language,escaped(output));QProcess process;process.start(ffmpeg(),{"-hide_banner","-loglevel","error","-y","-i",mediaPath,"-vn","-af",filter,"-f","null","-"});if(!process.waitForStarted(5000)||!process.waitForFinished(60*60*1000)||process.exitCode()!=0){process.kill();fail(error,QString::fromUtf8(process.readAllStandardError()).trimmed());return{};}QFile file(output);if(!file.open(QIODevice::ReadOnly)){fail(error,"Whisper did not create transcript JSON.");return{};}return parseWhisperJson(file.readAll(),error);
}

std::vector<Rational> MediaAnalysis::detectOnsetsFromPcm(const std::vector<float>& samples,int sampleRate){
    if(sampleRate<=0||samples.empty())return{};const int window=std::max(128,sampleRate/100);std::vector<double> energy;for(std::size_t pos=0;pos<samples.size();pos+=window){double sum=0;for(std::size_t i=pos;i<std::min(samples.size(),pos+window);++i)sum+=samples[i]*samples[i];energy.push_back(std::sqrt(sum/std::max<std::size_t>(1,std::min(samples.size(),pos+window)-pos)));}
    std::vector<Rational> result;int last=-100;for(std::size_t i=4;i+2<energy.size();++i){double mean=0;for(std::size_t j=i-4;j<i;++j)mean+=energy[j];mean/=4;const double flux=energy[i]-mean;if(energy[i]>.025&&flux>std::max(.018,mean*.55)&&energy[i]>=energy[i+1]&&static_cast<int>(i)-last>=8){result.emplace_back(static_cast<std::int64_t>(i*window),sampleRate);last=static_cast<int>(i);}}return result;
}

std::vector<Rational> MediaAnalysis::detectOnsets(const QString& mediaPath,QString* error){QProcess process;process.start(ffmpeg(),{"-hide_banner","-loglevel","error","-i",mediaPath,"-vn","-ac","1","-ar","16000","-f","f32le","-"});if(!process.waitForStarted(5000)||!process.waitForFinished(10*60*1000)||process.exitCode()!=0){process.kill();fail(error,QString::fromUtf8(process.readAllStandardError()).trimmed());return{};}const auto bytes=process.readAllStandardOutput();std::vector<float> samples(static_cast<std::size_t>(bytes.size()/4));if(!samples.empty())std::memcpy(samples.data(),bytes.constData(),samples.size()*4);return detectOnsetsFromPcm(samples,16000);}

std::vector<TrackingPoint> MediaAnalysis::trackGrayFrames(const std::vector<QByteArray>&frames,int width,int height,double x,double y,double regionWidth,double regionHeight,double frameSeconds){std::vector<TrackingPoint> result;if(frames.empty()||width<8||height<8||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(regionWidth)||!std::isfinite(regionHeight)||!std::isfinite(frameSeconds)||regionWidth<=0||regionHeight<=0||frameSeconds<=0)return result;const auto frameSize=static_cast<qsizetype>(width)*height;if(std::any_of(frames.begin(),frames.end(),[&](const auto&frame){return frame.size()<frameSize;}))return result;int boxW=std::clamp(static_cast<int>(regionWidth*width),4,width/2),boxH=std::clamp(static_cast<int>(regionHeight*height),4,height/2);int px=std::clamp(static_cast<int>(x*width),0,width-boxW),py=std::clamp(static_cast<int>(y*height),0,height-boxH);QByteArray templ(boxW*boxH,0);for(int row=0;row<boxH;++row)std::memcpy(templ.data()+row*boxW,frames.front().constData()+(py+row)*width+px,boxW);result.push_back({.time=Rational{},.x=static_cast<double>(px)/width,.y=static_cast<double>(py)/height,.confidence=1});for(std::size_t index=1;index<frames.size();++index){const auto&frame=frames[index];double best=1e30;int bestX=px,bestY=py;for(int cy=std::max(0,py-10);cy<=std::min(height-boxH,py+10);cy+=2)for(int cx=std::max(0,px-10);cx<=std::min(width-boxW,px+10);cx+=2){double sad=0;for(int row=0;row<boxH;row+=2)for(int col=0;col<boxW;col+=2)sad+=std::abs(static_cast<unsigned char>(frame[(cy+row)*width+cx+col])-static_cast<unsigned char>(templ[row*boxW+col]));if(sad<best){best=sad;bestX=cx;bestY=cy;}}px=bestX;py=bestY;const double samples=std::max(1,(boxW/2)*(boxH/2));result.push_back({.time=Rational{static_cast<std::int64_t>(std::llround(index*frameSeconds*1000)),1000},.x=static_cast<double>(px)/width,.y=static_cast<double>(py)/height,.confidence=std::clamp(1.0-best/(samples*255.0),0.0,1.0)});}return result;}
std::vector<TrackingPoint> MediaAnalysis::trackRegion(const QString&mediaPath,Rational start,Rational duration,int sourceWidth,int sourceHeight,double x,double y,double regionWidth,double regionHeight,QString*error){if(sourceWidth<=0||sourceHeight<=0||duration<=Rational{}){fail(error,"Tracking requires video dimensions and duration.");return{};}const int width=160;int height=std::max(8,static_cast<int>(std::llround(static_cast<double>(sourceHeight)*width/sourceWidth)));height-=height%2;QProcess process;process.start(ffmpeg(),{"-hide_banner","-loglevel","error","-ss",QString::number(static_cast<double>(start.asLongDouble()),'f',6),"-t",QString::number(std::min(30.0,static_cast<double>(duration.asLongDouble())),'f',6),"-i",mediaPath,"-an","-vf",QString("fps=10,scale=%1:%2,format=gray").arg(width).arg(height),"-f","rawvideo","-"});if(!process.waitForStarted(5000)||!process.waitForFinished(10*60*1000)||process.exitCode()!=0){process.kill();fail(error,QString::fromUtf8(process.readAllStandardError()).trimmed());return{};}const auto raw=process.readAllStandardOutput();const int frameSize=width*height;std::vector<QByteArray>frames;for(qsizetype offset=0;offset+frameSize<=raw.size();offset+=frameSize)frames.push_back(raw.mid(offset,frameSize));auto result=trackGrayFrames(frames,width,height,x,y,regionWidth,regionHeight,.1);if(result.empty())fail(error,"No video frames were decoded for tracking.");return result;}
} // namespace ytp
