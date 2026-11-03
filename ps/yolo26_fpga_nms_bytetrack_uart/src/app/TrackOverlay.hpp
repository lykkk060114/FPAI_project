#pragma once
#include <opencv2/opencv.hpp>
#include <deque>
#include <map>
#include <set>
#include <cmath>
namespace xunjing {
// OpenCV drawing accepts component bytes, not BGR colors, on a RGB565 Mat.
inline cv::Scalar overlayColor(const cv::Mat& frame, cv::Scalar bgr) {
    if(frame.type()!=CV_8UC2) return bgr;
    const auto b=cv::saturate_cast<unsigned char>(bgr[0]);
    const auto g=cv::saturate_cast<unsigned char>(bgr[1]);
    const auto r=cv::saturate_cast<unsigned char>(bgr[2]);
    const unsigned packed=(b>>3)|((g>>2)<<5)|((r>>3)<<11);
    return {double(packed&255),double(packed>>8)};
}
inline void overlayLabel(cv::Mat& frame, const std::string& text, cv::Point at, cv::Scalar bgr) {
    int base=0; auto sz=cv::getTextSize(text,cv::FONT_HERSHEY_SIMPLEX,0.65,2,&base);
    at.x=std::max(0,std::min(at.x,std::max(0,frame.cols-sz.width-8)));
    at.y=std::clamp(at.y,sz.height+5,std::max(sz.height+5,frame.rows-base-5));
    cv::rectangle(frame,cv::Rect(at.x,at.y-sz.height-4,sz.width+8,sz.height+base+8),overlayColor(frame,{15,15,15}),-1);
    cv::putText(frame,text,at+cv::Point(4,0),cv::FONT_HERSHEY_SIMPLEX,0.65,overlayColor(frame,bgr),2,cv::LINE_8);
}
class TrackOverlay {
    std::map<int,std::deque<cv::Point>> history_;
public:
    template<class Tracks, class Label> void Draw(cv::Mat& frame,const Tracks& tracks,Label label) {
        std::set<int> active;
        for(const auto& t:tracks) {
            if(!std::isfinite(t.box.x)||!std::isfinite(t.box.y)||!std::isfinite(t.box.width)||!std::isfinite(t.box.height)) continue;
            cv::Rect r=cv::Rect(t.box)&cv::Rect(0,0,frame.cols,frame.rows);
            if(r.width<2||r.height<2) continue;
            active.insert(t.id);
            auto bgr=t.locked?cv::Scalar(0,220,255):cv::Scalar(80+(t.id*37)%160,220,80);
            auto c=overlayColor(frame,bgr);
            const int n=std::max(1,std::min({24,r.width/3,r.height/3}));
            const int thick=t.locked?3:2;
            for(int x:{r.x,r.x+r.width-1}) for(int y:{r.y,r.y+r.height-1}) {
                cv::line(frame,{x,y},{x+(x==r.x?n:-n),y},c,thick,cv::LINE_8);
                cv::line(frame,{x,y},{x,y+(y==r.y?n:-n)},c,thick,cv::LINE_8);
            }
            cv::Point center(r.x+r.width/2,r.y+r.height/2);
            auto& h=history_[t.id]; h.push_back(center); if(h.size()>16) h.pop_front();
            for(size_t i=1;i<h.size();++i) cv::line(frame,h[i-1],h[i],c,1,cv::LINE_8);
            cv::drawMarker(frame,center,c,cv::MARKER_CROSS,16,2,cv::LINE_8);
            overlayLabel(frame,label(t),{r.x,r.y-8},bgr);
        }
        for(auto it=history_.begin();it!=history_.end();) if(!active.count(it->first)) it=history_.erase(it); else ++it;
    }
};
}
