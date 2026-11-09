#include "core.h"
static Game g;
int main(void){
    WorldGenerate(&g, 20251109u);
    const char *ramp = " .:-=+*#%@";
    int W=78,H=36;
    for(int j=0;j<H;j++){
        char line[W+1];
        for(int i=0;i<W;i++){
            float x = -300.0f + 600.0f*(float)i/(float)(W-1);
            float z = -300.0f + 600.0f*(float)j/(float)(H-1);
            float h = WorldHeight(&g,x,z);
            char c;
            if(h<0){ float d=clampf(-h/26.0f,0,1); c = d<0.25f?',':(d<0.6f?'~':'='); }
            else c = ramp[(int)clampf(h/44.0f*(float)(strlen(ramp)-1),0,strlen(ramp)-1)];
            line[i]=c;
        }
        line[W]=0;
        printf("%s\n",line);
    }
    printf("terrain min %.2f max %.2f trees %d rocks %d reeds %d grass %d\n", g.terrain.minH,g.terrain.maxH,g.terrain.treeCount,g.terrain.rockCount,g.terrain.reedCount,g.terrain.grassCount);
    Landmarks *lm=&g.terrain.lm;
    printf("dock   %.1f %.1f yaw %.2f h=%.2f\n", lm->dockPos.x,lm->dockPos.z,lm->dockYaw,WorldHeight(&g,lm->dockPos.x,lm->dockPos.z));
    printf("boat   %.1f %.1f depth=%.2f\n", lm->boatStart.x,lm->boatStart.z,WorldWaterDepth(&g,lm->boatStart.x,lm->boatStart.z));
    printf("light  %.1f %.1f h=%.2f\n", lm->lighthouse.x,lm->lighthouse.z,lm->lighthouse.y);
    printf("camp   %.1f %.1f h=%.2f\n", lm->campPos.x,lm->campPos.z,lm->campPos.y);
    printf("shop   %.1f %.1f h=%.2f\n", lm->shopPos.x,lm->shopPos.z,lm->shopPos.y);
    printf("spawn  %.1f %.1f h=%.2f\n", lm->spawnPos.x,lm->spawnPos.z,lm->spawnPos.y);
    printf("depth at 0,250 = %.2f ; at 100,300 = %.2f ; at -250,0 = %.2f\n",
        WorldWaterDepth(&g,0,250),WorldWaterDepth(&g,100,300),WorldWaterDepth(&g,-250,0));
    return 0;
}
