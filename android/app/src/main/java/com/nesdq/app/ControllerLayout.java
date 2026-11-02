package com.nesdq.app;

import java.util.LinkedHashMap;
import java.util.Map;

/** 縦横の安全領域へゲーム画面と操作ボタンを配置する。 */
final class ControllerLayout {
    final Map<Integer,int[]> buttons=new LinkedHashMap<>();
    int[] screen;
    private void put(int key,float x,float y,float w,float h){buttons.put(key,new int[]{Math.round(x),Math.round(y),Math.max(1,Math.round(w)),Math.max(1,Math.round(h))});}
    ControllerLayout(int width,int height,float density){
        float w=width,h=height,g=8*density;
        float small=Math.min(40*density,h*.075f);
        if(w<=h){
            float top=small,bottom=44*density;
            float minimumKey=Math.min(w*.16f,44*density);
            float sh=Math.min(w*15/16,h-top-small-bottom-4*g-3*minimumKey);
            sh=Math.max(1,sh);float sw=Math.min(w,sh*16/15);
            screen=new int[]{Math.round((w-sw)/2),Math.round(top+g),Math.round(sw),Math.round(sh)};
            put(-1,g,0,w*.22f,top);put(8,w-w*.22f-g,0,w*.22f,top);
            float secondary=top+g+sh+g;
            put(10,g,secondary,w*.25f,small);put(9,w-w*.25f-g,secondary,w*.25f,small);
            float controlsTop=secondary+small+g,controlsBottom=h-bottom-g*2;
            float available=controlsBottom-controlsTop;
            float key=Math.min(Math.min(w*.175f,94*density),available/3.15f);
            float dx=w*.025f,dy=controlsTop+(available-3*key)/2;
            put(0,dx+key,dy,key,key);put(2,dx,dy+key,key,key);put(3,dx+2*key,dy+key,key,key);put(1,dx+key,dy+2*key,key,key);
            float diameter=Math.min(Math.min(w*.195f,104*density),available/2.5f);
            float right=w-g-diameter,left=right-diameter-w*.025f;
            float cluster=controlsTop+(available-diameter*2.35f)/2;
            put(6,left,cluster+diameter*.25f,diameter,diameter);put(7,right,cluster,diameter,diameter);
            put(5,left,cluster+diameter*1.35f,diameter,diameter);put(4,right,cluster+diameter*1.1f,diameter,diameter);
            
        }else{
            float sw=Math.min(w*.5f,(h-2*g)*16/15),sh=sw*15/16;
            float side=(w-sw)/2;
            screen=new int[]{Math.round(side),Math.round((h-sh)/2),Math.round(sw),Math.round(sh)};
            float bw=(side-3*g)/2;
            put(-1,g,g,bw,small);put(8,2*g+bw,g,bw,small);
            put(10,w-g-bw,g,bw,small);put(9,2*g+bw,small+3*g,bw,small);
            float key=Math.min(Math.min(Math.min(side*.32f,94*density),h*.198f),(side-g)/3);
            float dx=(side-3*key)/2,dy=Math.max(2*small+4*g,h*.35f);
            dy=Math.min(dy,h-g-3*key);
            put(0,dx+key,dy,key,key);put(2,dx,dy+key,key,key);put(3,dx+2*key,dy+key,key,key);put(1,dx+key,dy+2*key,key,key);
            float diameter=Math.min(side*.41f,h*.21f),rightStart=side+sw;
            float left=rightStart+side*.05f,right=rightStart+side*.52f,cluster=h*.34f;
            put(6,left,cluster+h*.05f,diameter,diameter);put(7,right,cluster,diameter,diameter);
            put(5,left,cluster+h*.29f,diameter,diameter);put(4,right,cluster+h*.24f,diameter,diameter);
            
        }
    }
}
