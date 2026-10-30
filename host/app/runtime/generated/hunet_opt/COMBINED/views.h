#pragma once
#include "../../../device/hunet_opt_views.h"
static const OptViewSegment opt_segments_2_2[]={
{0,6,2231959872,0,24,64,64,0,0},
};
static const OptViewSegment opt_segments_2_4[]={
{0,6,2232746688,0,6,64,64,0,0},
{6,18,2231959872,6,24,64,64,0,0},
};
static const OptViewSegment opt_segments_4_2[]={
{0,6,2238350784,0,24,64,64,0,0},
};
static const OptViewSegment opt_segments_4_4[]={
{0,6,2239137600,0,6,64,64,0,0},
{6,18,2238350784,6,24,64,64,0,0},
};
static const OptViewSegment opt_segments_7_2[]={
{0,12,2245331776,0,48,32,32,0,0},
};
static const OptViewSegment opt_segments_7_4[]={
{0,12,2245725376,0,12,32,32,0,0},
{12,36,2245331776,12,48,32,32,0,0},
};
static const OptViewSegment opt_segments_9_2[]={
{0,12,2248527808,0,48,32,32,0,0},
};
static const OptViewSegment opt_segments_9_4[]={
{0,12,2248921408,0,12,32,32,0,0},
{12,36,2248527808,12,48,32,32,0,0},
};
static const OptView opt_views[]={
{2u,2u,6u,64u,64u,1u,opt_segments_2_2},
{2u,4u,24u,64u,64u,2u,opt_segments_2_4},
{4u,2u,6u,64u,64u,1u,opt_segments_4_2},
{4u,4u,24u,64u,64u,2u,opt_segments_4_4},
{7u,2u,12u,32u,32u,1u,opt_segments_7_2},
{7u,4u,48u,32u,32u,2u,opt_segments_7_4},
{9u,2u,12u,32u,32u,1u,opt_segments_9_2},
{9u,4u,48u,32u,32u,2u,opt_segments_9_4},
};
static inline const OptView*opt_bound_view(uint32_t block,uint32_t node){for(const auto&v:opt_views)if(v.block_id==block&&v.node_index==node)return &v;return nullptr;}
static inline bool opt_virtual_node(uint32_t block,uint32_t node){return (block==2u&&node==0u)||(block==2u&&node==1u)||(block==2u&&node==3u)||(block==4u&&node==0u)||(block==4u&&node==1u)||(block==4u&&node==3u)||(block==7u&&node==0u)||(block==7u&&node==1u)||(block==7u&&node==3u)||(block==9u&&node==0u)||(block==9u&&node==1u)||(block==9u&&node==3u);}
