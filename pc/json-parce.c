#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "json-c/json.h"

const
char *layername[]={
  "event",
  "ground",
  "object1",
  "object2",
};

/*
 * +-------+
 * | event |
 * +-------+
 * | ground|
 * +-------+
 * |object1|
 * +-------+
 * |object2|
 * +-------+
 *
 */

#define LOCAL_SIZ 5
#define LAYERS 4

#define LOCAL_MAPS 25

typedef struct{
  uint16_t map[LAYERS*LOCAL_SIZ*LOCAL_SIZ];
} localmap_t;

typedef struct{
  uint16_t width; // divided by 5, 
  uint16_t height;
  char name[64];
  localmap_t localmap[1];//expands accordingly.
} map_t;
  
int main(int argc,char **argv){
  FILE *fp;
  int ic = 1;
  uint16_t tmp;
  int width;
  int height;
  map_t *m;

  while(ic < argc){
    struct json_object *jo = json_object_from_file(argv[ic]);
    struct json_object *layers;
    struct json_object *jwidth;
    struct json_object *jheight;
    json_object_object_get_ex(jo,"layers",&layers);
    json_object_object_get_ex(jo,"width",&jwidth);
    json_object_object_get_ex(jo,"height",&jheight);
    width = json_object_get_int(jwidth)/5;
    height = json_object_get_int(jheight)/5;
    json_object_put(jwidth);
    json_object_put(jheight);
    
    m = calloc(1,sizeof(map_t)+sizeof(localmap_t)*(width*height-1));
    m->width = width;
    m->height = height;
    for (int i = 0; i < json_object_array_length(layers); i++) {
      struct json_object *layer = json_object_array_get_idx(layers,i);
      struct json_object *name;
      struct json_object *data;
      json_object_object_get_ex(layer,"name",&name);
      json_object_object_get_ex(layer,"data",&data);
      fprintf(stderr,"%s\n",json_object_get_string(name));
      int lyc;
      {
	int i;
	for(i=0;i<4;i++){
	  if(!strcmp(json_object_get_string(name),layername[i]))break;
	}
	if(i==4)continue;
	lyc = i;
	fprintf(stderr,"%d\n",lyc);
      }
      for (int i = 0; i < json_object_array_length(data); ++i) {
	int tx,ty,dx,dy;
	tx = (i / LOCAL_SIZ) % width;
	ty = (i / LOCAL_SIZ) / width / LOCAL_SIZ;
	dx = i%LOCAL_SIZ;
	dy = i/LOCAL_SIZ/width%LOCAL_SIZ;
	struct json_object *a = json_object_array_get_idx(data, i);
	tmp = json_object_get_int(a);
	m->localmap[tx+ty*width].map[dx+dy*LOCAL_SIZ+lyc*LOCAL_SIZ*LOCAL_SIZ] = tmp;
	json_object_put(a);
      }
      json_object_put(data);
      json_object_put(name);
      json_object_put(layer);
    }
    json_object_put(layers);
    json_object_put(jo);
    char *filename;
    filename = malloc(strlen(argv[ic])+4+1);
    strcpy(filename,argv[ic]);
    {
      int i;
      while(filename[i]!='.')i++;
      strcpy(filename+i,".omap");
      fprintf(stderr,"write to %s\n",filename);
    }
    fp = fopen(filename,"w");
    fwrite(m,sizeof(map_t)+sizeof(localmap_t)*(width*height-1),1,fp);
    fclose(fp);
    ic++;
  }
}
