#include <xc.h>
#include "ff.h"
#include <stdint.h>
#include "rotatevideo.h"
#include "xprintf.h"
#include "graphlib.h"
#include "NVMem.h"

//外付けクリスタル with PLL (20/3倍)
//クリスタルは3.579545×4＝14.31818MHz
#pragma config FSRSSEL = PRIORITY_7
#pragma config PMDL1WAY = OFF
#pragma config IOL1WAY = OFF
//#pragma config FUSBIDIO = OFF
//#pragma config FVBUSONIO = OFF
#pragma config FPLLIDIV = DIV_3
#pragma config FPLLMUL = MUL_20
//#pragma config UPLLIDIV = DIV_1
//#pragma config UPLLEN = OFF
#pragma config FPLLODIV = DIV_1
#pragma config FNOSC = PRIPLL
#pragma config FSOSCEN = OFF
#pragma config IESO = OFF
#pragma config POSCMOD = XT
#pragma config OSCIOFNC = OFF
#pragma config FPBDIV = DIV_1
#pragma config FCKSM = CSDCMD
#pragma config FWDTEN = OFF
#pragma config DEBUG = OFF
#pragma config PWP = OFF
#pragma config BWP = OFF
#pragma config CP = OFF

FATFS fatfs;
FIL fmc;
FIL fmp;


typedef int16_t maptileid_t;

typedef struct{
  maptileid_t *map;
  uint8_t isvisible;
  uint8_t transparent_color;
} map_layer_t;

typedef struct{
  map_layer_t *ml;
  uint16_t width;
  uint16_t height;
} map_t;


/*
 * layer0 ... event
 * layer1 ... draw (mainly for ground)
 * layer2 ... draw (mainly for object1)
 * layer3 ... draw (mainly for object2)
 */

#define LOCAL_SIZ 5
#define LAYERS 4

#define LOCAL_MAPS 30


/*
 * Flash Storage for Mapchips.
 *
 * +--------------+
 * | cache zone   |
 * | status       |
 * | (1013*4=4052)|
 * +--------------+
 * | image storage|
 * |(1013*256=    |
 * |      259328) |
 * ~~~~~~~~~~~~~~~~~
 * |              |
 * +--------------+
 *
 */

typedef struct{
  uint16_t map[LAYERS*LOCAL_SIZ*LOCAL_SIZ];
  int16_t x,y;
  int dist;
  uint8_t isalive;
} local_map_t;

local_map_t lmaps[LOCAL_MAPS];  

#define TILE_WIDTH 16
#define TILE_HEIGHT 16

#define MAPCHIP_HASH_BLANK -1

FIL *fimg = &fmc;
unsigned char palette[224*3];


#define COLOR_USE 224
#define COLOR_OFS 16
#define TILE_W 16
#define TILE_H 16
#define TILE_SIZ (TILE_W*TILE_H)

#define window_height 256
#define window_width 256

#define SIZEOF_IMGSTORAGE 0x1000*65
// we should use prime number for this.
#define NUMOF_MAXIMG 1021

#define FFx16 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
#define FFx256 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16 FFx16
#define FFx4096 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256 FFx256
#define FFx4096x16 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096 FFx4096

const uint8_t _imgstorage[SIZEOF_IMGSTORAGE+0x1000]={
   FFx4096x16 FFx4096x16 FFx4096x16 FFx4096x16 FFx4096 FFx4096
};

const struct{
  int32_t status[NUMOF_MAXIMG];
  uint32_t img[NUMOF_MAXIMG*TILE_SIZ/4];
} *imgstorage;// actually zone aligned 0x1000.

// 入力ボタンのポート、ビット定義
#define KEYPORT PORTD
#define KEYDOWN 0x0001
#define KEYLEFT 0x0002
#define KEYUP 0x0004
#define KEYRIGHT 0x0008
#define KEYSTART 0x0010
#define KEYFIRE 0x0020

static inline
int mapchip_hash(maptileid_t id){
  return id % NUMOF_MAXIMG;
}

static inline
int mapchip_rehash(maptileid_t id){
  return mapchip_hash(id+1);
}

//return 255 if data was not found.
static inline
int search_idx(maptileid_t id){
  int idx = mapchip_hash(id);
  int t=NUMOF_MAXIMG;

  while(imgstorage->status[idx]!=id&&imgstorage->status[idx]!=MAPCHIP_HASH_BLANK&&--t){
    idx = mapchip_rehash(idx);
  }

  /* printf("%d/%d(%d)\n",id,idx,imgstorage->status[idx]); */
    return (imgstorage->status[idx]==id&&t)?idx:-1;
}

static
int get_idx_to_insert_mapchip(maptileid_t id){
  int idx;

  idx = mapchip_hash(id);
  while(imgstorage->status[idx]!=MAPCHIP_HASH_BLANK){
    idx = mapchip_rehash(idx);
    if(idx==mapchip_hash(id))break;
  }

  if(idx==mapchip_hash(id)&&imgstorage->status[idx]!=MAPCHIP_HASH_BLANK){
    return -1; //No blank.
  }

  return idx;
}



static
int add_mapchip(int idx,maptileid_t id){
  uint32_t buff[TILE_SIZ/4];
  uint32_t *p=&imgstorage->img[idx*TILE_SIZ/4];
  unsigned int read;

  f_lseek(fimg,sizeof(palette)+(id-1)*TILE_W*TILE_H);
  f_read(fimg,buff,TILE_SIZ,&read);
  {
    int i;
  for(i=0;i<TILE_SIZ/4;i++){
    NVMemWriteWord(&p[i],buff[i]);
  }
  }

  NVMemWriteWord(&imgstorage->status[idx],id);
  
  return idx;
  return -1;
}

int getmapchip(maptileid_t id){
  int idx;

  idx = search_idx(id);
  if(idx == -1){
    idx = get_idx_to_insert_mapchip(id);
    if(idx == -1){
      /* printf("map hash exhausted"); */
      return -1;
    }
    return add_mapchip(idx,id);
  }
  return idx;
}

static inline
int draw_mapchip(int idx,int px,int py,int transp){
  int x,y;

  if(idx==-1){
    return -1;
  }

  if(px<=-TILE_W)return 0;
  if(px>=window_width+TILE_W)return 0;
  if(py<=-TILE_H)return 0;
  if(py>=window_height+TILE_H)return 0;

  if(transp){
    int dx;
    int dy;
    for(dy=0;dy<TILE_H;dy++){
      for( dx=0;dx<TILE_W;dx++){
	x = px+dx;
	y = py+dy;
	if(((uint8_t*)imgstorage->img)[idx*TILE_SIZ+dx+dy*TILE_W]-16==154)continue;
	VRAM[x+(y)*256] = ((uint8_t*)imgstorage->img)[idx*TILE_SIZ+dx+dy*TILE_W];
      }
    }
  }else{
    int dx;
    int dy;
    for(dy=0;dy<TILE_H;dy++){
      for(dx=0;dx<TILE_W;dx++){
	x = (px+dx);
	y = py+dy;
	VRAM[x+(y)*256] = ((uint8_t*)imgstorage->img)[idx*TILE_SIZ+dx+dy*TILE_W];
      }
    }
  }
  return 0;
}

typedef struct{
  uint16_t w; //width, divided by 5, 
  uint16_t h;
  char name[64];
} map_headder_t;

map_headder_t mh;

int sofs_x=30;
int sofs_y=120;

int init_mapdata(void){
  unsigned int read;
  char str[256];
  f_read(&fmp,&mh,sizeof(mh),&read);
  xsprintf(str,"map[%d,%d](%d)\n",mh.w,mh.h,read);
  printstr(0,60,7,str);
  
  return 0;
}

void loadlocalarea(local_map_t *p,int x,int y){
  unsigned int read;
  f_lseek(&fmp,sizeof(map_headder_t)+(x+y*mh.w)*LOCAL_SIZ*LOCAL_SIZ*sizeof(uint16_t)*LAYERS);
  f_read(&fmp,p,sizeof(uint16_t)*LOCAL_SIZ*LOCAL_SIZ*LAYERS,&read);
  p->x = x;
  p->y = y;
  p->isalive = 1;
}

int calcofs_x(local_map_t *p){
  return p->x*LOCAL_SIZ*TILE_W;
}

int calcofs_y(local_map_t *p){
  return p->y*LOCAL_SIZ*TILE_H;
}

void drawlocalarea(local_map_t *p){
  int i,j,k=1,x,y;
  int ofsx,ofsy;
  if(!p->isalive)return;

  ofsx = calcofs_x(p);
  ofsy = calcofs_y(p);
  
  for(i=0;i<LOCAL_SIZ;i++){
    y=(ofsy+i*TILE_H)%256;
    for(j=0;j<LOCAL_SIZ;j++){
      x=(ofsx+j*TILE_W)%256;
      if(p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]&&p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]<1550){
	int idx = getmapchip(p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]);
	if(idx==-1)return;
	if(ofsy+i*TILE_H-sofs_y >= 0&&ofsy+i*TILE_H-sofs_y < window_height&&
	   ofsx+j*TILE_W-sofs_x >= 0&&ofsx+j*TILE_W-sofs_x < window_width)
	  draw_mapchip(idx,x,y,0);
      }
    }
  }

  for(k=2;k<LAYERS;k++){
    for(i=0;i<LOCAL_SIZ;i++){
      y=(ofsy+i*TILE_H)%256;
      for(j=0;j<LOCAL_SIZ;j++){
	x=(ofsx+j*TILE_W)%256;
	if(p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]&&p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]<1550){
	  int idx = getmapchip(p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]);
	  if(idx==-1)return;
	  if(ofsy+i*TILE_H-sofs_y >= 0&&ofsy+i*TILE_H-sofs_y < window_height&&
	     ofsx+j*TILE_W-sofs_x >= 0&&ofsx+j*TILE_W-sofs_x < window_width)
	    draw_mapchip(idx,x,y,1);
	}
      }
    }
  }
}

void drawlocalarea_ij(local_map_t *p,int i,int j){
  int k=1,x,y;
  int ofsx,ofsy;
  if(!p->isalive)return;

  ofsx = calcofs_x(p);
  ofsy = calcofs_y(p);
  
  y=(ofsy+i*TILE_H)%256;
  x=(ofsx+j*TILE_W)%256;
  if(p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]&&p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]<1550){
    int idx = getmapchip(p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]);
    if(idx==-1)return;
    if(ofsy+i*TILE_H-sofs_y >= 0&&ofsy+i*TILE_H-sofs_y < window_height&&
       ofsx+j*TILE_W-sofs_x >= 0&&ofsx+j*TILE_W-sofs_x < window_width)
      draw_mapchip(idx,x,y,0);
  }

  for(k=2;k<LAYERS;k++){
    y=(ofsy+i*TILE_H)%256;
    x=(ofsx+j*TILE_W)%256;
    if(p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]&&p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]<1550){
      int idx = getmapchip(p->map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j]);
      if(idx==-1)return;
      if(ofsy+i*TILE_H-sofs_y >= 0&&ofsy+i*TILE_H-sofs_y < window_height&&
	 ofsx+j*TILE_W-sofs_x >= 0&&ofsx+j*TILE_W-sofs_x < window_width)
	draw_mapchip(idx,x,y,1);
    }
  }
}

static
int calc_distance(local_map_t *p){
  int dx,dy;
  int cx,cy;

  cx = (sofs_x+window_width/2)/TILE_W;
  cy = (sofs_y+window_height/2)/TILE_H;

  dx = p->x*LOCAL_SIZ+LOCAL_SIZ/2-cx;
  dy = p->y*LOCAL_SIZ+LOCAL_SIZ/2-cy;

  p->dist = dx*dx+dy*dy;

  return p->dist;
}

const int loadorder[LOCAL_SIZ]={0,1,-1,2,-2};
void load_segment(void){
  int i,j,k,cx,cy,x,y,mi=0;

  {
    int i;
    for(i=0;i<LOCAL_MAPS;i++){
      calc_distance(&lmaps[i]);
      if((lmaps[mi].isalive&&lmaps[mi].dist < lmaps[i].dist)||lmaps[i].isalive==0)
	mi = i;
    }
  }
  cx = (sofs_x+window_width/2)/TILE_W;
  cx /= LOCAL_SIZ;
  cy = (sofs_y+window_height/2)/TILE_H;
  cy /= LOCAL_SIZ;
  for(j=0;j<LOCAL_SIZ;j++){
    y = loadorder[j]+cy;
    if(y<0||y>=mh.h)continue;
    for(i=0;i<LOCAL_SIZ;i++){
      x = loadorder[i]+cx;
      if(x<0||x>=mh.w)continue;
      for(k=0;k<LOCAL_MAPS;k++){
	if(lmaps[k].isalive&&lmaps[k].x==x&&lmaps[k].y==y)break;
      }
      /* printf("%d\n",k); */
      if(k==LOCAL_MAPS){
	goto load;
      }
    }
  }
  goto next;
 load:
  /* printf("loading(%d,%d)\n",x,y); */
  loadlocalarea(lmaps+mi,x,y);
 next:
  return;
}

void draw_map(void){
  {
    load_segment();
  }
  {
    int i;
    for(i=0;i<LOCAL_MAPS;i++){
      /* printf("(%d,%d),",lmaps[i].x,lmaps[i].y); */
      drawlocalarea(lmaps+i);
    }
    /* puts(""); */
  }
}

#define DIRUP 1
#define DIRDOWN 2
#define DIRRIGHT 4
#define DIRLEFT 8


int redraw(int dirflag){
  int k;
  int ofsx,ofsy;
  for(k=0;k<LOCAL_MAPS;k++){
    ofsx = calcofs_x(&lmaps[k]);
    ofsy = calcofs_y(&lmaps[k]);
    
    if(dirflag&DIRUP){
      if(ofsx-sofs_x>-LOCAL_SIZ*TILE_W&&ofsx-sofs_x<window_width+TILE_W*2&&
	 ofsy-sofs_y<TILE_H&&ofsy-sofs_y>=-TILE_H*(LOCAL_SIZ-1)){
	int i,j;
	//ofsy+i*TILE_H-sofs_y >= 0&&ofsy+i*TILE_H-sofs_y < window_height
	i = ((sofs_y-ofsy+(TILE_H-1))/TILE_H)%LOCAL_SIZ;
	for(j=0;j<LOCAL_SIZ;j++)
	  drawlocalarea_ij(&lmaps[k],i,j);
	/* drawlocalarea(&lmaps[k]); */
      }
    }
    if(dirflag&DIRLEFT){
      if(ofsy-sofs_y>-LOCAL_SIZ*TILE_H&&ofsy-sofs_y<window_height+TILE_H&&
	 ofsx-sofs_x<TILE_W&&ofsx-sofs_x>=-TILE_W*(LOCAL_SIZ-1)){
	int i,j;
	//ofsy+i*TILE_H-sofs_y >= 0&&ofsy+i*TILE_H-sofs_y < window_height
	j = ((sofs_x-ofsx+(TILE_W-1))/TILE_W)%LOCAL_SIZ;
	for(i=0;i<LOCAL_SIZ;i++)
	  drawlocalarea_ij(&lmaps[k],i,j);
	/* drawlocalarea(&lmaps[k]); */
      }
    }

    if(dirflag&DIRDOWN){
      if(ofsx-sofs_x>-LOCAL_SIZ*TILE_W&&ofsx-sofs_x<window_width+TILE_W*2&&
	 ofsy-sofs_y>window_height-TILE_H*LOCAL_SIZ&&ofsy-sofs_y<=window_height){
	int i,j;
	i = ((sofs_y-ofsy+window_height)/TILE_H)%LOCAL_SIZ;
	for(j=0;j<5;j++)
	  drawlocalarea_ij(&lmaps[k],i,j);
      }
    }

    if(dirflag&DIRRIGHT){
      if(ofsy-sofs_y>-LOCAL_SIZ*TILE_H&&ofsy-sofs_y<window_height+TILE_H&&
	 ofsx-sofs_x>window_width-TILE_W*LOCAL_SIZ&&ofsx-sofs_x<=window_width){
	int j,i;
	j = ((sofs_x-ofsx+window_width)/TILE_W)%LOCAL_SIZ;
	for(i=0;i<5;i++)
	  drawlocalarea_ij(&lmaps[k],i,j);
      }
    }
  }

  {
    int i,j,k,m,id,idx;
    for(m=0;m<LOCAL_MAPS;m++){
      for(k=1;k<LAYERS;k++){
	if(lmaps[m].isalive){
	  for(i=0;i<LOCAL_SIZ;i++){
	    for(j=0;j<LOCAL_SIZ;j++){
	      id = lmaps[m].map[(k*LOCAL_SIZ+i)*LOCAL_SIZ+j];
	      if(id==0)continue;
	      idx = getmapchip(id);
	      if(idx==-1)continue;
	    }
	  }
	}
      }
    }
  }
    
  return 0;
}

void wait60thsec(int n){
  int i;
  for(i=0;i<n;i++){
    while(!drawing);
    while(drawing);
  }
}

int main(void){
  int i;
  unsigned int read;
  FRESULT res;  

  imgstorage = (void*)((uint32_t)(_imgstorage)&~0x1000);
  
  TRISB = 0x0000;						// 全て出力
  TRISC = 0x0000;						// 全て出力
  TRISD = KEYSTART | KEYFIRE | KEYUP | KEYDOWN | KEYLEFT | KEYRIGHT;// ボタン接続ポート入力設定
  TRISE = 0x0000;						// 全て出力
  TRISF = 0x0003;						// RF0,1は入力
  TRISG = 0x0080;						// RG7は入力
  ANSELD = 0x0000; // 全てデジタル
  ANSELG = 0x0000; // 全てデジタル
  CNPUDSET=KEYSTART | KEYFIRE | KEYUP | KEYDOWN | KEYLEFT | KEYRIGHT;// プルアップ設定

  SDI2R = 1;						//RPG7にSDI2を割り当て
  RPG8R = 6;						//RPG8にSDO2を割り当て

  init_composite(); // ビデオメモリクリア、割り込み初期化、カラービデオ出力開始
  vscanv1_x=(256-16);
  vscanv1_y=0;
  vscanv2_x=0;
  vscanv2_y=(256-16);

  printstr(0,0,7,"SD INIT BEGIN");

  if ((res = f_mount(&fatfs, "", 1)) != FR_OK) {
    printstr(0,20,7,"SD INIT ERR");
    while (1) ;
  }
  printstr(0,30,7,"SD INIT SUCCESS");
  
  res=f_open(&fmc,"mapchip.dat",FA_READ);
  if(res){
    printstr(0,50,7,"mapchip cannot open");
    printnum(200,50,7,res);
    while (1) ;    
  }

  res=f_open(&fmp,"mapdata.dat",FA_READ);
  if(res){
    printstr(0,50,7,"mapdata cannot open");
    printnum(200,50,7,res);
    while (1) ;    
  }

  /* wait60thsec(120); */
  f_read(&fmc,palette,sizeof(palette),&read);
  for(i=0;i<224;i++){
    set_palette(i+16,palette[i*3+2],palette[i*3],palette[i*3+1]);
  }
  printstr(0,50,7,"palette loaded");
  /* wait60thsec(120); */

  init_mapdata();
  printstr(0,50,7,"init map data");
  /* wait60thsec(120); */
  
  printstr(0,50,7,"init map cache");
  /* wait60thsec(60); */
  for(i=0;i<20;i++)draw_map();
  while(1){
    int redrawflg = 0;
    if(~KEYPORT&KEYRIGHT){
      redrawflg |= DIRRIGHT;
      if(sofs_x+3+window_width < mh.w*TILE_W*LOCAL_SIZ)
	sofs_x+=3;
    }
    if(~KEYPORT&KEYLEFT){
      redrawflg |= DIRLEFT;
      if(sofs_x > 3)
	sofs_x-=3;
    }
    if(~KEYPORT&KEYDOWN){
      if(sofs_y+3+window_height < mh.h*TILE_H*LOCAL_SIZ)
	sofs_y+=3;
      redrawflg |= DIRDOWN;
    }
    if(~KEYPORT&KEYUP){
      if(sofs_y > 3)
	sofs_y-=3;
      redrawflg |= DIRUP;
    }
    vscanstartx = (sofs_x+TILE_W)*256;
    vscanstarty = (sofs_y+TILE_H)*256;
    redraw(redrawflg);
    load_segment();
    wait60thsec(1);
  }
  return 0;
}
