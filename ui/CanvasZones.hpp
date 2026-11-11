#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace soundnet::canvas {

enum class Stage { Notes, Sources, Effects, Output, Free };
inline constexpr int stageCount=int(Stage::Free);

struct StageExtent {
    int count=0;
    double first=0, last=0, sum=0;

    void add(double x) {
        if(!std::isfinite(x))return;
        first=count?std::min(first,x):x;
        last=count?std::max(last,x):x;
        sum+=x;
        ++count;
    }
    void translate(double dx) {
        if(!count)return;
        first+=dx;
        last+=dx;
        sum+=dx*count;
    }
};

struct Zone {
    double left=0, right=0;
    bool occupied=false;
    double center() const {return (left+right)*.5;}
};

struct Zones {
    std::array<Zone,stageCount> bands{};
    bool empty=true;

    // Empty stages can start at any sensible position between occupied stages.
    // Leave room for free placement outside the occupied part of the canvas.
    double placementX(Stage stage,double x) const {
        if(empty || stage==Stage::Free)return x;
        const int index=int(stage);
        double left=-std::numeric_limits<double>::infinity(),right=-left;
        for(int i=index-1;i>=0;--i)if(bands[i].occupied){left=bands[i].right;break;}
        for(int i=index+1;i<stageCount;++i)if(bands[i].occupied){right=bands[i].left;break;}
        if(x>=left && x<=right)return x;
        const double margin=std::min(26.,(right-left)*.25);
        return std::clamp(x,left+margin,right-margin);
    }
};

using Extents=std::array<StageExtent,stageCount>;
using ZoneShifts=std::array<double,stageCount>;
inline constexpr double automaticZoneShift=56;

struct ZoneInsertion {
    double center=0;
    ZoneShifts shifts{};
    bool fits=true;
};

// Reserve a full card and its side margins between adjacent occupied stages.
// Only the extra space not already available in the gap displaces a stage.
inline ZoneInsertion planZoneInsertion(const Extents& extents,Stage stage,double wanted,
                                       double spacing,double maxShift=automaticZoneShift) {
    if(stage==Stage::Free)return {wanted};
    const int index=int(stage);
    int left=index-1,right=index+1;
    while(left>=0 && !extents[left].count)--left;
    while(right<stageCount && !extents[right].count)++right;
    const double infinity=std::numeric_limits<double>::infinity();
    const double lower=left>=0?extents[left].last+spacing:-infinity;
    const double upper=right<stageCount?extents[right].first-spacing:infinity;
    if(lower<=upper)return {std::clamp(wanted,lower,upper)};
    if(lower-upper>2*maxShift)return {std::clamp(wanted,upper,lower),{},false};

    const double first=std::max(upper,lower-maxShift),last=std::min(lower,upper+maxShift);
    ZoneInsertion best;
    double bestCost=infinity;
    const auto consider=[&](double candidate) {
        const double center=std::clamp(candidate,first,last);
        ZoneInsertion plan{center};
        double cost=0;
        const auto makeRoom=[&](int i,int direction,double amount) {
            while(i>=0 && i<stageCount && amount>0) {
                plan.shifts[i]=direction*amount;
                cost+=amount*extents[i].count;
                int next=i+direction;
                while(next>=0 && next<stageCount && !extents[next].count)next+=direction;
                if(next<0 || next>=stageCount)break;
                const double gap=direction<0?extents[i].first-extents[next].last:
                    extents[next].first-extents[i].last;
                amount-=std::max(0.,gap-spacing);
                i=next;
            }
        };
        makeRoom(left,-1,lower-center);
        makeRoom(right,1,center-upper);
        if(cost<bestCost || (cost==bestCost && std::abs(center-wanted)<std::abs(best.center-wanted))) {
            best=plan;
            bestCost=cost;
        }
    };
    consider(wanted);
    consider(first);
    consider(last);
    // The cost changes slope where an outer stage starts to move.
    for(int direction:{-1,1}) {
        int inner=direction<0?left:right;
        double slack=0;
        for(int i=inner+direction;i>=0 && i<stageCount;i+=direction) {
            if(!extents[i].count)continue;
            const double gap=direction<0?extents[inner].first-extents[i].last:
                extents[i].first-extents[inner].last;
            slack+=std::max(0.,gap-spacing);
            consider(direction<0?lower-slack:upper+slack);
            inner=i;
        }
    }
    return best;
}

inline Zones makeZones(const std::array<StageExtent,stageCount>& extents) {
    constexpr double spacing=240, minimumGap=76;
    Zones result;
    std::array<double,stageCount> anchors{};
    for(int i=0;i<stageCount;++i) {
        if(!extents[i].count)continue;
        result.empty=false;
        anchors[i]=extents[i].sum/extents[i].count;
    }
    if(result.empty)return result;
    for(int i=0;i<stageCount;++i) {
        if(extents[i].count)continue;
        int left=i-1,right=i+1;
        while(left>=0 && !extents[left].count)--left;
        while(right<stageCount && !extents[right].count)++right;
        if(left>=0 && right<stageCount)
            anchors[i]=anchors[left]+(anchors[right]-anchors[left])*(i-left)/(right-left);
        else if(left>=0)anchors[i]=anchors[left]+spacing*(i-left);
        else anchors[i]=anchors[right]-spacing*(right-i);
    }

    // Project the anchors onto an ordered sequence. Older documents may have
    // their stages in any order. Move the derived boundaries, not those cards.
    struct Block {int start,end;double sum,weight;};
    std::array<Block,stageCount> blocks{};
    int blockCount=0;
    for(int i=0;i<stageCount;++i) {
        const double weight=extents[i].count?std::sqrt(extents[i].count):.25;
        blocks[blockCount++]={i,i,(anchors[i]-minimumGap*i)*weight,weight};
        while(blockCount>1) {
            auto& a=blocks[blockCount-2];
            const auto& b=blocks[blockCount-1];
            if(a.sum/a.weight<=b.sum/b.weight)break;
            a.end=b.end;a.sum+=b.sum;a.weight+=b.weight;--blockCount;
        }
    }
    for(int j=0;j<blockCount;++j) {
        const auto& block=blocks[j];
        for(int i=block.start;i<=block.end;++i)anchors[i]=block.sum/block.weight+minimumGap*i;
    }

    double boundary=std::min(anchors.front()-spacing/2,
                             extents.front().count?extents.front().first-110:anchors.front());
    for(int i=0;i<stageCount;++i) {
        auto& band=result.bands[i];
        band.left=boundary;
        band.occupied=extents[i].count>0;
        if(i+1<stageCount) {
            const double left=extents[i].count?std::max(anchors[i],extents[i].last):anchors[i];
            const double right=extents[i+1].count?std::min(anchors[i+1],extents[i+1].first):anchors[i+1];
            boundary=left<=right?(left+right)*.5:(anchors[i]+anchors[i+1])*.5;
            boundary=std::clamp(boundary,anchors[i]+minimumGap/4,anchors[i+1]-minimumGap/4);
        } else {
            boundary=std::max(anchors[i]+spacing/2,
                              extents[i].count?extents[i].last+110:anchors[i]);
        }
        band.right=boundary;
    }
    // A stage without cards occupies no visible space between its neighbors.
    // Split their free gap at the midpoint so it belongs to those two stages.
    int previous=-1;
    for(int i=0;i<stageCount;++i) {
        if(!extents[i].count)continue;
        if(previous>=0 && i-previous>1) {
            const double left=std::max(anchors[previous],extents[previous].last);
            const double right=std::min(anchors[i],extents[i].first);
            const double middle=left<=right?(left+right)*.5:(anchors[previous]+anchors[i])*.5;
            const double boundary=std::clamp(middle,anchors[previous]+minimumGap/4,anchors[i]-minimumGap/4);
            result.bands[previous].right=boundary;
            result.bands[i].left=boundary;
            for(int j=previous+1;j<i;++j)result.bands[j].left=result.bands[j].right=boundary;
        }
        previous=i;
    }
    return result;
}

} // namespace soundnet::canvas
