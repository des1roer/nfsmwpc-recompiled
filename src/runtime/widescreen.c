/* Resolution and HUD adaptation from ThirteenAG's MIT WidescreenFix.
 * Update the original HUD constants and centers, not the x87 camera stack. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "nfs_widescreen.h"
#ifdef __ANDROID__
#include <android/log.h>
#define HUD_LOG(...) __android_log_print(ANDROID_LOG_INFO,"NFSMW",__VA_ARGS__)
#else
#define HUD_LOG(...) (fprintf(stderr,"[NFSMW:HUD] " __VA_ARGS__),fputc('\n',stderr))
#endif
static unsigned render_width,render_height;
static float display_aspect;
static float hud_center=320.f;
static void put_float(uint32_t address,float value){uint32_t bits;memcpy(&bits,&value,sizeof(value));MEM32(address)=bits;}
static void update_hud(void){
    float aspect=display_aspect>0.f?display_aspect:4.f/3.f;
    hud_center=240.f*aspect;
    put_float(0x008AF9A4u,1.f/hud_center);
    put_float(0x00894B40u,hud_center);
}

void nfs_widescreen_init(const char *game_root) { (void)game_root; }
void nfs_widescreen_set_display_aspect(float aspect) { display_aspect=aspect>1.f&&aspect<8.f?aspect:0.f; }
void nfs_widescreen_set_resolution(unsigned width, unsigned height) { render_width=width;render_height=height; }
int nfs_widescreen_hud_entries(void) { return 0; }
/* Minimap vertical move (Android: the bottom-left corner sits under the
 * steering thumb). Minimap::AdjustForWidescreen (0x5678A0) moves the map
 * horizontally through its local X (+0xB8) and five FE children (+0x3C, +0x40,
 * +0x44, +0x48, +0x90; object data +0x2C, X at +0x1C); the same objects carry Y
 * at +0xBC and data +0x20, and the radar icons add the pivot pair at 0x91CF04
 * (X) / 0x91CF08 (Y, otherwise unused and 0). */
static float minimap_offset;
/* Android "top-left" layout: the map also moves left by the widescreen margin
 * (hud_center - 320), keeping its 4:3 distance to the left edge of the screen. */
static int minimap_left;
static uint32_t minimap_object;
static float minimap_applied,minimap_written_y,minimap_applied_x;
static float pivot_base,pivot_written;
static float minimap_offset_x(void){return minimap_left&&hud_center>320.f?320.f-hud_center:0.f;}
static float get_float(uint32_t address){float value;uint32_t bits=MEM32(address);memcpy(&value,&bits,sizeof(value));return value;}
/* The round frame (and other art centred on the map) is not referenced by the
 * Minimap. TRACK_MAP0..3 are tiles inside an FE group of the HUD package; the
 * frame is a sibling of that group. Packages: FEngine = [[0x91CADC]], package
 * lists with heads at +0xE0 and +0xF0 (FEPackage: next +4, name +0xC, objects
 * +0x4C). FEObject: next +4, NameHash +0x10, Type +0x18 (5 = group, children
 * +0x64), data +0x2C (X +0x1C, Y +0x20). Every sibling of the map group
 * centred on it, that the Minimap does not place itself, moves with the map.
 * The first pass logs that list for diagnosis. */
static int minimap_frame_logged;
static int minimap_owns(uint32_t minimap,uint32_t object){
    for(uint32_t k=0x30u;k<0x130u;k+=4u)if(MEM32(minimap+k)==object)return 1;
    return 0;
}
static int guest_names_equal(uint32_t a,uint32_t b){
    for(unsigned i=0;i<128u;++i){
        int x=MEM8(a+i),y=MEM8(b+i);
        if(x>='a'&&x<='z')x-=32;
        if(y>='a'&&y<='z')y-=32;
        if(x!=y)return 0;
        if(!x)return 1;
    }
    return 0;
}
static uint32_t hud_package(uint32_t hud){
    uint32_t name=MEM32(hud+0x24u),fe=MEM32(0x0091CADCu),engine=fe?MEM32(fe):0;
    if(!name||!engine)return 0;
    static const uint32_t heads[]={0xe0u,0xf0u};
    for(unsigned h=0;h<2u;++h){
        unsigned guard=0;
        for(uint32_t package=MEM32(engine+heads[h]);package&&guard<256u;package=MEM32(package+4u),++guard){
            uint32_t packageName=MEM32(package+0xcu);
            if(packageName&&guest_names_equal(packageName,name))return package;
        }
    }
    return 0;
}
/* The first object of the list holding the group whose children start with
 * tile (depth-first, as FEPackage::ForAllObjects at 0x5B1560). */
static uint32_t find_map_group(uint32_t first,uint32_t tile,uint32_t* list,unsigned depth,unsigned* budget){
    for(uint32_t object=first;object&&*budget;object=MEM32(object+4u),--*budget){
        if(MEM32(object+0x18u)!=5u)continue;
        uint32_t children=MEM32(object+0x64u);
        if(children==tile){*list=first;return object;}
        if(depth<8u&&children){uint32_t found=find_map_group(children,tile,list,depth+1u,budget);if(found)return found;}
    }
    return 0;
}
/* HUD package objects can outlive a Minimap: remember what was written so a
 * later pass starts from the original Y instead of moving them twice. */
static struct {uint32_t data;float original_x,original,written_x,written;} frame_moves[64];
static void move_frame_object(uint32_t data){
    float x=get_float(data+0x1cu),y=get_float(data+0x20u);
    unsigned slot=0;
    for(;slot<64u&&frame_moves[slot].data;++slot)if(frame_moves[slot].data==data)break;
    if(slot==64u)slot=63u;
    if(frame_moves[slot].data!=data||y!=frame_moves[slot].written||x!=frame_moves[slot].written_x){
        frame_moves[slot].data=data;frame_moves[slot].original=y;frame_moves[slot].original_x=x;}
    frame_moves[slot].written=frame_moves[slot].original+minimap_offset;
    frame_moves[slot].written_x=frame_moves[slot].original_x+minimap_offset_x();
    put_float(data+0x20u,frame_moves[slot].written);
    put_float(data+0x1cu,frame_moves[slot].written_x);
}
/* Objects positioned (in absolute HUD units: groups add their position to
 * their children's) within 40 units of the map centre, outside the map group,
 * move with the map: the dark disc and the frame live in other HUD groups. */
static void move_centred(uint32_t first,float ox,float oy,float cx,float cy,uint32_t group,uint32_t minimap,float dy,
                         unsigned depth,unsigned* moved,unsigned* count){
    for(uint32_t object=first;object&&*count<2048u;object=MEM32(object+4u)){
        ++*count;
        uint32_t data=MEM32(object+0x2cu);
        if(object==group||!data||minimap_owns(minimap,object))continue;
        float x=ox+get_float(data+0x1cu),y=oy+get_float(data+0x20u);
        uint32_t type=MEM32(object+0x18u);
        int near=x>cx-40.f&&x<cx+40.f&&y>cy-40.f&&y<cy+40.f;
        if(!minimap_frame_logged)HUD_LOG("minimap hud %*s%08x type=%u at %.1f,%.1f%s",(int)(depth*2u),"",MEM32(object+0x10u),type,x,y,near?" -> moved":"");
        if(near){move_frame_object(data);++*moved;continue;}
        if(type==5u&&depth<6u)move_centred(MEM32(object+0x64u),x,y,cx,cy,group,minimap,dy,depth+1u,moved,count);
    }
}
static void move_minimap_frame(uint32_t hud,uint32_t minimap,float dy){
    uint32_t tile=MEM32(minimap+0x3cu),package=hud_package(hud);
    if(!tile||!package){if(!minimap_frame_logged)HUD_LOG("minimap frame: HUD package not found");minimap_frame_logged=1;return;}
    while(MEM32(tile+8u))tile=MEM32(tile+8u);  /* first child of the map group */
    unsigned budget=8192u;uint32_t list=0;
    uint32_t group=find_map_group(MEM32(package+0x4cu),tile,&list,0u,&budget);
    uint32_t groupData=group?MEM32(group+0x2cu):0;
    if(!groupData){if(!minimap_frame_logged)HUD_LOG("minimap frame: map group not found in the HUD package");minimap_frame_logged=1;return;}
    float cx=get_float(groupData+0x1cu),cy=get_float(groupData+0x20u);
    unsigned moved=0,count=0;
    move_centred(list,0.f,0.f,cx,cy,group,minimap,dy,0u,&moved,&count);
    if(!minimap_frame_logged)HUD_LOG("minimap group %08x at %.1f,%.1f: %u objects visited, %u moved with the map",MEM32(group+0x10u),cx,cy,count,moved);
    minimap_frame_logged=1;
}
static void move_minimap(uint32_t hud,uint32_t minimap,float dx,float dy){
    move_minimap_frame(hud,minimap,dy);
    /* As Minimap::AdjustForWidescreen (0x5678A0) for X: local X and all five children. */
    static const uint32_t children[]={0x3cu,0x40u,0x44u,0x48u,0x90u};
    for(unsigned i=0;i<sizeof(children)/sizeof(children[0]);++i){
        uint32_t child=MEM32(minimap+children[i]),data=child?MEM32(child+0x2cu):0;
        if(data&&children[i]!=0x90u)put_float(data+0x20u,get_float(data+0x20u)+dy);
        if(data)put_float(data+0x1cu,get_float(data+0x1cu)+dx);
    }
    put_float(minimap+0xb8u,get_float(minimap+0xb8u)+dx);
    put_float(minimap+0xbcu,get_float(minimap+0xbcu)+dy);
    { uint32_t child=MEM32(minimap+0x90u),data=child?MEM32(child+0x2cu):0;
      if(data)MEM32(data+0x20u)=MEM32(minimap+0xbcu); }
}
/* Once per HUD update (FEngHud::SetWideScreenMode entry, ecx = FEngHud). A new
 * Minimap, or one at a reused address whose Y no longer holds what we wrote,
 * starts from the original layout. */
static void update_minimap(uint32_t hud){
    uint32_t minimap=hud?MEM32(hud+0x320u):0;
    if(!minimap){minimap_object=0;return;}
    if(minimap!=minimap_object||get_float(minimap+0xbcu)!=minimap_written_y){minimap_object=minimap;minimap_applied=0.f;minimap_applied_x=0.f;}
    float target_x=minimap_offset_x();
    if(minimap_applied!=minimap_offset||minimap_applied_x!=target_x){
        move_minimap(hud,minimap,target_x-minimap_applied_x,minimap_offset-minimap_applied);
        minimap_applied=minimap_offset;minimap_applied_x=target_x;
    }
    minimap_written_y=get_float(minimap+0xbcu);
    put_float(0x0091CF08u,minimap_applied);
    /* The radar icons' X pivot: the game sets it (0 or -120) on a widescreen mode
     * change; keep its value and add the horizontal move on top. */
    float pivot=get_float(0x0091CF04u);
    if(pivot!=pivot_written)pivot_base=pivot;
    pivot_written=pivot_base+minimap_applied_x;
    put_float(0x0091CF04u,pivot_written);
}
void nfs_widescreen_set_minimap_offset(float dy) { minimap_offset=dy; }
void nfs_widescreen_set_minimap_left(int on) { minimap_left=on!=0; }
void nfs_widescreen_set_frame_rate(unsigned fps) { (void)fps; }
void nfs_widescreen_apply_frame_rate(void) {}
void nfs_frontend_cursor_units(int on) { (void)on; }
float nfs_widescreen_hud_pos_x(void) { return hud_center; }
/* FixFOV from ThirteenAG's NFSMostWanted.WidescreenFix (MIT), Scaling = 1.
 * The projection (0x6CF400) takes the vertical angle as the horizontal one
 * times the viewport's height/width, so the wider the screen the narrower the
 * vertical view (Vert-): on 20:9 phones the car filled the screen, more so as
 * the speed FOV widened. Like the mod: keep the 4:3 vertical view and widen
 * the horizontal one (Hor+) for the player views 1 and 4; the rear-view mirror
 * (view 3) uses 0.4, reflections (views > 10) keep their projection. */
static double fov_scale[3]={1.0,0.5,1.0};
static void fix_fov_view(uint32_t view){
    float aspect=display_aspect>0.f?display_aspect:4.f/3.f;
    double hor=1.0/(aspect/(4.0/3.0))/1.047485948;
    if(view==1||view==4){fov_scale[0]=hor;fov_scale[1]=0.43434;fov_scale[2]=1.21;}
    else{fov_scale[0]=1.0;fov_scale[1]=0.5;fov_scale[2]=1.0;}
    g_st[0]=view==3?0.4:1.0;
}

/* Android cheats and the speed overlay (nfs_widescreen.h). Infinite nitrous
 * follows Odell's 1.3 trainer (github.com/odell0111/nfs-mostwanted-trainer),
 * which turns both `fld [esi+0xF8]` reads of the charge into `fld 1.0`; here the
 * charge itself is refilled before each read, so no x86 byte changes. */
static volatile int infinite_nitro;
static volatile uint32_t speedometer_object,speedometer_calls;
void nfs_cheat_infinite_nitro(int on){infinite_nitro=on!=0;}
uint32_t nfs_speedometer_object(uint32_t* calls){if(calls)*calls=speedometer_calls;return speedometer_object;}

void recomp_hook(uint32_t va) {
    switch(va){
    /* Speedometer HUD update (vtable 0x8A21D4 slot 1): this = ecx; it shows
     * fabs(m/s at +0x48) * 3.6 (km/h) or * 2.237 (mph). */
    case 0x0057A540u:speedometer_object=g_ecx;speedometer_calls++;return;
    /* Nitrous charge (0..1) at vehicle+0xF8: read for the empty check, then
     * drained by this frame's use and stored back. */
    case 0x00692A96u:case 0x00692AFAu:if(infinite_nitro)put_float(g_esi+0xF8u,1.f);return;
    /* strncpy-like copy (dest, src, count): the localized-string lookup at
     * 0x56BB80 returns NULL when the player's LANGUAGES files lack the entry
     * (other game editions), and the copy read guest address 0. Copy "". */
    case 0x00460CA0u:
        if(!MEM32(g_esp+8)&&MEM32(g_esp+12)&&MEM32(g_esp+4)){MEM8(MEM32(g_esp+4))=0;MEM32(g_esp+8)=MEM32(g_esp+4);}
        return;
    /* ST0 = viewport height/width, view id in ecx. */
    case 0x006CF4F6u:fix_fov_view(g_ecx);return;
    /* `fdivr [0x89096C]`, `fmul [0x8933D4]`, `fdivr [0x89096C]`: rescale by new/original. */
    case 0x006CF56Cu:g_st[0]*=fov_scale[0]/get_float(0x0089096Cu);return;
    case 0x006CF57Eu:g_st[0]*=fov_scale[1]/get_float(0x008933D4u);return;
    case 0x006CF5E2u:g_st[0]*=fov_scale[2]/get_float(0x0089096Cu);return;
    case 0x006C27F9u:case 0x006C2810u:case 0x006C2827u:
    case 0x006C283Eu:case 0x006C2855u:case 0x006C286Cu:
        if(render_width&&render_height){
            MEM32(MEM32(g_esp+4))=render_width;
            MEM32(MEM32(g_esp+8))=render_height;
        }
        update_hud();
        return;
    case 0x005696C0u:update_minimap(g_ecx);return;
    /* After each original mov [esp+offset],320, before its following Y write. */
    case 0x0056FED8u:put_float(g_esp+0x19c,hud_center);return;
    case 0x00584EFAu:put_float(g_esp+0x90,hud_center);return;
    case 0x00599E84u:put_float(g_esp+0x84,hud_center);return;
    case 0x0059A12Bu:put_float(g_esp+0xc4,hud_center);return;
    case 0x0059A5B6u:case 0x0059A849u:put_float(g_esp+0x94,hud_center);return;
    case 0x005A44D0u:put_float(g_esp+0x64,hud_center);return;
    /* Original GetCursorPos conversion returns 0..640. HUD coordinates now
     * span 0..2*center; change the result before the game computes deltas. */
    case 0x006C9C5Cu:MEM32(g_esi+4)=(uint32_t)((float)(int32_t)MEM32(g_esi+4)*(hud_center/320.f));return;
    }
    fprintf(stderr, "Unexpected disabled MW render hook at 0x%08X\n", (unsigned)va);
    abort();
}
