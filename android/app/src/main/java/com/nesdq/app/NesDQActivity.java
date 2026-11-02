package com.nesdq.app;

import org.libsdl.app.SDLActivity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.drawable.GradientDrawable;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.RelativeLayout;
import android.widget.Toast;
import java.io.*;
import java.util.HashSet;
import java.util.Set;

public class NesDQActivity extends SDLActivity {
    private static final int PICK_ROM=1, PICK_FOLDER=2;
    private static native void openRom(byte[] utf8);
    private static native void touchKey(int action, boolean down);
    private SharedFolder storage;
    private SaveQueue saveQueue;
    private RelativeLayout controls;
    private int safeLeft,safeTop,safeRight,safeBottom,layoutWidth,layoutHeight;
    private final Set<Integer> heldButtons=new HashSet<>();
    private volatile boolean gameActive;
    private boolean pickerBusy;
    private android.widget.LinearLayout romPicker;
    private android.widget.ListView romList;
    private java.util.List<SharedFolder.RomEntry> romEntries;
    private int romSelected;
    private int romMotionAction=-1;
    private final android.os.Handler romInputHandler=new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable repeatRomMotion=new Runnable(){
        @Override public void run(){
            if(romPicker==null || romMotionAction<0)return;
            navigateRom(romMotionAction);
            romInputHandler.postDelayed(this,100);
        }
    };
    private void resetRomMotion(){
        romMotionAction=-1;
        romInputHandler.removeCallbacks(repeatRomMotion);
    }
    public void requestRomSelection(){runOnUiThread(()->{if(storageReady())chooseRom();else chooseFolder();});}
    public void romConsumed(byte[] utf8){
        File file=new File(new String(utf8,java.nio.charset.StandardCharsets.UTF_8));
        try{
            File parent=file.getParentFile();
            if(parent!=null && getCacheDir().equals(parent.getParentFile()) && parent.getName().startsWith("nesdq-rom-")){
                file.delete();parent.delete();
            }
            else if(getCacheDir().equals(parent) && file.getName().startsWith("nesdq-rom-"))file.delete();
        }
        catch(SecurityException ignored){}
    }
    public boolean storageReady(){return storage!=null && storage.ready();}
    public boolean storageExists(String path)throws IOException{storageFlush();return storage.exists(path);}
    public void storageMkdir(String path)throws IOException{storageFlush();storage.mkdir(path);}
    public byte[] storageRead(String path,int limit)throws IOException{storageFlush();return storage.read(path,limit);}
    public void storageWrite(String path,byte[] bytes)throws IOException{saveQueue.write(path,bytes);}
    public String storageFailure(){return saveQueue.failure();}
    public void storageFlush()throws IOException{saveQueue.flush();}
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    @Override protected String[] getLibraries(){return new String[]{"SDL3","main"};}
    private void styleButton(Button button, boolean round) {
        GradientDrawable background = new GradientDrawable();
        background.setShape(round ? GradientDrawable.OVAL : GradientDrawable.RECTANGLE);
        if (!round) background.setCornerRadius(dp(3));
        background.setColor(round ? 0xffa3333f : 0xff344154);
        // The platform Button background has top/bottom insets. On short
        // landscape buttons those leave only a thin strip behind the label.
        button.setBackgroundTintList(null);
        button.setBackground(new android.graphics.drawable.RippleDrawable(
                ColorStateList.valueOf(0x40ffffff), background, null));
        button.setStateListAnimator(null);
        button.setElevation(0);
        button.setGravity(android.view.Gravity.CENTER);
        button.setPadding(0, 0, 0, 0);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setTextColor(Color.WHITE);
        button.setTypeface(null, Typeface.BOLD);
        fitButtonLabel(button);
    }

    private void fitButtonLabel(Button button) {
        final float preferredSize = button.getTextSize();
        button.setSingleLine(true);
        button.setIncludeFontPadding(false);
        button.setMinHeight(0);
        button.setMinimumHeight(0);
        // Measure with Android's actual font, including the user's font scale.
        // Landscape utility buttons can be much shorter than their portrait size.
        button.addOnLayoutChangeListener((view, l, t, r, b, ol, ot, or, ob) -> {
            int width = r - l - button.getCompoundPaddingLeft() - button.getCompoundPaddingRight() - dp(4);
            int height = b - t - button.getCompoundPaddingTop() - button.getCompoundPaddingBottom() - dp(2);
            if (width <= 0 || height <= 0) return;
            android.text.TextPaint paint = new android.text.TextPaint(button.getPaint());
            paint.setTextSize(preferredSize);
            CharSequence text = button.getText();
            if (button.getTransformationMethod() != null)
                text = button.getTransformationMethod().getTransformation(text, button);
            android.graphics.Paint.FontMetrics metrics = paint.getFontMetrics();
            float scale = Math.min(1f, Math.min(width / Math.max(1f, paint.measureText(text.toString())),
                    height / Math.max(1f, metrics.bottom - metrics.top)));
            float size = preferredSize * scale;
            if (Math.abs(button.getTextSize() - size) > 0.1f)
                button.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX, size);
        });
    }

    private Button keyButton(String label, int action) {
        Button button = new Button(this);
        button.setText(label);
        if(action==6)button.setContentDescription("Select");
        if(action==7)button.setContentDescription("Start");
        button.setTextSize(action <= 3 ? 36 : action <= 5 ? 30 : action <= 7 ? 20 : 14);
        styleButton(button, action >= 4 && action <= 7);
        button.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    if(romPicker!=null){navigateRom(action);view.setPressed(true);return true;}
                    heldButtons.add(action);
                    touchKey(action, true);
                    view.setPressed(true);
                    return true;
                case MotionEvent.ACTION_MOVE:
                    if (event.getX() < 0 || event.getY() < 0 || event.getX() >= view.getWidth() || event.getY() >= view.getHeight()) {
                        if (heldButtons.remove(action)) touchKey(action, false);
                        view.setPressed(false);
                    }
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    if (heldButtons.remove(action)) touchKey(action, false);
                    view.setPressed(false);
                    return true;
                default: return true;
            }
        });
        return button;
    }

    private void releaseButtons() {
        for (int action : heldButtons) touchKey(action, false);
        heldButtons.clear();
    }

    @Override protected void onPause() {
        resetRomMotion();
        releaseButtons();
        super.onPause();
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        if (!focused) resetRomMotion();
        if (!focused) releaseButtons();
        super.onWindowFocusChanged(focused);
    }


    private void place(View view,int[] box){
        RelativeLayout.LayoutParams p=new RelativeLayout.LayoutParams(box[2],box[3]);
        p.leftMargin=box[0];p.topMargin=box[1];controls.addView(view,p);
    }
    private void arrange(){
        if(controls==null || mLayout.getWidth()==0 || mLayout.getHeight()==0)return;
        int width=Math.max(1,mLayout.getWidth()-safeLeft-safeRight);
        int height=Math.max(1,mLayout.getHeight()-safeTop-safeBottom);
        if(width==layoutWidth && height==layoutHeight)return;
        layoutWidth=width;layoutHeight=height;releaseButtons();
        RelativeLayout.LayoutParams p=new RelativeLayout.LayoutParams(width,height);
        p.leftMargin=safeLeft;p.topMargin=safeTop;controls.setLayoutParams(p);
        controls.removeAllViews();
        ControllerLayout layout=new ControllerLayout(width,height,getResources().getDisplayMetrics().density);
        int[] box=layout.screen;
        RelativeLayout.LayoutParams surface=new RelativeLayout.LayoutParams(box[2],box[3]);
        surface.leftMargin=safeLeft+box[0];surface.topMargin=safeTop+box[1];
        android.view.ViewGroup.LayoutParams previous=mSurface.getLayoutParams();
        if(!(previous instanceof RelativeLayout.LayoutParams) || previous.width!=surface.width || previous.height!=surface.height ||
            ((RelativeLayout.LayoutParams)previous).leftMargin!=surface.leftMargin || ((RelativeLayout.LayoutParams)previous).topMargin!=surface.topMargin)
            mSurface.setLayoutParams(surface);
        String[] labels={"↑","↓","←","→","A","B","SEL","START","MENU","SAVE 1","LOAD 1"};
        for(java.util.Map.Entry<Integer,int[]> entry:layout.buttons.entrySet()){
            int action=entry.getKey();
            if(action>=0)place(keyButton(labels[action],action),entry.getValue());
            else{
                Button file=new Button(this);file.setText("ROM");file.setTextSize(14);styleButton(file,false);fitButtonLabel(file);
                file.setOnClickListener(v->{
                    if(gameActive){touchKey(11,true);touchKey(11,false);return;}
                    if(storageReady())chooseRom();else chooseFolder();
                });place(file,entry.getValue());
            }
        }
        if(romPicker!=null)place(romPicker,layout.screen);
    }
    private void chooseFolder(){
        releaseButtons();
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_WRITE_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        if(Build.VERSION.SDK_INT>=26)intent.putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,
            Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADocuments"));
        startActivityForResult(intent,PICK_FOLDER);
    }
    private void chooseRom(){
        if(pickerBusy)return;
        pickerBusy=true;
        releaseButtons();
        new Thread(()->{
            try{
                java.util.List<SharedFolder.RomEntry> entries=storage.roms();
                runOnUiThread(()->showRomList(entries));
            }catch(Exception error){runOnUiThread(()->{pickerBusy=false;showError(error);});}
        },"NesDQ-ROM-list").start();
    }
    private void showRomList(java.util.List<SharedFolder.RomEntry> entries){
        romEntries=entries;romSelected=0;
        romPicker=new android.widget.LinearLayout(this);
        romPicker.setOrientation(android.widget.LinearLayout.VERTICAL);
        romPicker.setBackgroundColor(0xff101722);
        android.widget.TextView title=new android.widget.TextView(this);
        title.setText("ROM選択　↑↓: 移動　A: 決定　B: 戻る");title.setTextColor(Color.WHITE);title.setTextSize(16);
        romPicker.addView(title);
        romList=new android.widget.ListView(this);
        java.util.List<String> names=new java.util.ArrayList<>();
        for(SharedFolder.RomEntry entry:entries)names.add(entry.name);
        names.add("別の場所から選ぶ…");names.add(gameActive?"戻る":"終了");
        romList.setAdapter(new android.widget.ArrayAdapter<String>(this,android.R.layout.simple_list_item_1,names){
            @Override public View getView(int position,View recycled,android.view.ViewGroup parent){
                android.widget.TextView row=(android.widget.TextView)super.getView(position,recycled,parent);
                row.setTextColor(Color.WHITE);
                row.setBackgroundColor(position==romSelected?0xff344f70:Color.TRANSPARENT);
                return row;
            }
        });
        romList.setSelector(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        romList.setOnItemClickListener((parent,view,position,id)->{romSelected=position;confirmRom();});
        romPicker.addView(romList,new android.widget.LinearLayout.LayoutParams(-1,0,1));
        ControllerLayout layout=new ControllerLayout(layoutWidth,layoutHeight,getResources().getDisplayMetrics().density);
        place(romPicker,layout.screen);
    }
    private void closeRomList(){
        resetRomMotion();
        if(romPicker!=null)controls.removeView(romPicker);
        romPicker=null;romList=null;romEntries=null;pickerBusy=false;
    }
    private void confirmRom(){
        int selected=romSelected,count=romEntries.size();
        Uri uri=selected<count?romEntries.get(selected).uri:null;
        closeRomList();
        if(uri!=null){pickerBusy=true;importRom(uri);}
        else if(selected==count)chooseRomFile();
        else if(!gameActive)finish();
    }
    private void navigateRom(int action){
        if(romPicker==null)return;
        int count=romEntries.size()+2;
        if(action==0 || action==2)romSelected=(romSelected+count-1)%count;
        else if(action==1 || action==3)romSelected=(romSelected+1)%count;
        else if(action==4){confirmRom();return;}
        else if(action==5){closeRomList();if(!gameActive)finish();return;}
        // ラジオボタンのアニメーションや、表示範囲内での再スクロールを避ける。
        int first=romList.getFirstVisiblePosition();
        for(int i=0;i<romList.getChildCount();++i)
            romList.getChildAt(i).setBackgroundColor(first+i==romSelected?0xff344f70:Color.TRANSPARENT);
        int index=romSelected-first;
        View row=index>=0 && index<romList.getChildCount()?romList.getChildAt(index):null;
        if(romSelected<first || (row!=null && row.getTop()<0))romList.setSelectionFromTop(romSelected,0);
        else if(row==null || row.getBottom()>romList.getHeight()){
            int height=row!=null?row.getHeight():romList.getChildCount()>0?romList.getChildAt(0).getHeight():dp(48);
            romList.setSelectionFromTop(romSelected,Math.max(0,romList.getHeight()-height));
        }
    }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent event){
        // 十字キーをキーイベントではなくHAT軸で通知するパッドにも対応する。
        if(romPicker!=null && event.isFromSource(android.view.InputDevice.SOURCE_JOYSTICK) &&
                event.getActionMasked()==MotionEvent.ACTION_MOVE){
            float x=event.getAxisValue(MotionEvent.AXIS_HAT_X);
            float y=event.getAxisValue(MotionEvent.AXIS_HAT_Y);
            if(Math.abs(x)<0.5f && Math.abs(y)<0.5f){
                x=event.getAxisValue(MotionEvent.AXIS_X);
                y=event.getAxisValue(MotionEvent.AXIS_Y);
            }
            float threshold=romMotionAction<0?0.5f:0.35f;
            int action=-1;
            if(Math.max(Math.abs(x),Math.abs(y))>threshold){
                if(Math.abs(y)>=Math.abs(x))action=y<0?0:1;
                else action=x<0?2:3;
            }
            if(action!=romMotionAction){
                resetRomMotion();
                romMotionAction=action;
                if(action>=0){
                    navigateRom(action);
                    romInputHandler.postDelayed(repeatRomMotion,350);
                }
            }
            return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        if(romPicker!=null){
            int action;
            switch(event.getKeyCode()){
                case KeyEvent.KEYCODE_DPAD_UP:action=0;break;
                case KeyEvent.KEYCODE_DPAD_DOWN:action=1;break;
                case KeyEvent.KEYCODE_DPAD_LEFT:action=2;break;
                case KeyEvent.KEYCODE_DPAD_RIGHT:action=3;break;
                case KeyEvent.KEYCODE_BUTTON_B:case KeyEvent.KEYCODE_X:case KeyEvent.KEYCODE_ENTER:
                case KeyEvent.KEYCODE_DPAD_CENTER:action=4;break;
                case KeyEvent.KEYCODE_BUTTON_A:case KeyEvent.KEYCODE_Z:case KeyEvent.KEYCODE_BACK:
                case KeyEvent.KEYCODE_ESCAPE:case KeyEvent.KEYCODE_DEL:action=5;break;
                default:return super.dispatchKeyEvent(event);
            }
            android.view.InputDevice device=event.getDevice();
            if(device!=null && device.getVendorId()==0x057e && device.getProductId()==0x2009 &&
                    (event.getKeyCode()==KeyEvent.KEYCODE_BUTTON_A || event.getKeyCode()==KeyEvent.KEYCODE_BUTTON_B))
                action=action==4?5:4;
            if(event.getAction()==KeyEvent.ACTION_DOWN && (event.getRepeatCount()==0 || action<4))navigateRom(action);
            if(event.getAction()==KeyEvent.ACTION_UP)super.dispatchKeyEvent(event);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
    private void chooseRomFile(){
        if(pickerBusy)return;
        pickerBusy=true;
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);intent.addCategory(Intent.CATEGORY_OPENABLE);intent.setType("*/*");
        try{
            if(Build.VERSION.SDK_INT>=26)intent.putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,storage.romFolder());
            startActivityForResult(intent,PICK_ROM);
        }catch(Exception error){pickerBusy=false;showError(error);}
    }
    private void showError(Exception error){
        runOnUiThread(()->new AlertDialog.Builder(this).setTitle("NesDQ")
            .setMessage(error.getMessage()).setPositiveButton("再試行",(d,w)->{if(storageReady())chooseRom();else chooseFolder();})
            .setNegativeButton(gameActive?"戻る":"終了",(d,w)->{if(!gameActive)finish();}).show());
    }
    @Override protected void onCreate(Bundle state){
        super.onCreate(state);
        storage=new SharedFolder(this);
        saveQueue=new SaveQueue((path,bytes)->storage.write(path,bytes),message->runOnUiThread(()->Toast.makeText(this,message,Toast.LENGTH_LONG).show()));
        if(mLayout==null)return;
        mLayout.setBackgroundColor(0xff101722);
        controls=new RelativeLayout(this);controls.setMotionEventSplittingEnabled(true);
        mLayout.addView(controls,new RelativeLayout.LayoutParams(-1,-1));
        mLayout.setOnApplyWindowInsetsListener((v,insets)->{
            int left,top,right,bottom;
            if(Build.VERSION.SDK_INT>=30){
                android.graphics.Insets bars=insets.getInsetsIgnoringVisibility(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
                android.graphics.Insets gestures=insets.getInsets(WindowInsets.Type.mandatorySystemGestures());
                left=Math.max(bars.left,gestures.left);right=Math.max(bars.right,gestures.right);
                top=Math.max(bars.top,gestures.top);bottom=Math.max(bars.bottom,gestures.bottom)+dp(8);
            }else{
                left=insets.getStableInsetLeft();top=insets.getStableInsetTop();right=insets.getStableInsetRight();bottom=insets.getStableInsetBottom()+dp(8);
            }
            // 同じInsetsの通知でSurfaceを再配置し、描画と音声を止めない。
            if(left!=safeLeft || top!=safeTop || right!=safeRight || bottom!=safeBottom){
                safeLeft=left;safeTop=top;safeRight=right;safeBottom=bottom;
                layoutWidth=layoutHeight=0;arrange();
            }
            return insets;
        });
        mLayout.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->arrange());
        mLayout.post(()->{
            mLayout.requestApplyInsets();arrange();
            if(storageReady())chooseRom();
            else new AlertDialog.Builder(this).setTitle("保存先の指定")
                .setMessage("Documentsを選択すると、その中にNesDQ/Rom・Save・stateを作成します。既存のNesDQフォルダーも選択できます。")
                .setPositiveButton("フォルダーを選ぶ",(d,w)->chooseFolder()).setNegativeButton("終了",(d,w)->finish()).setCancelable(false).show();
        });
    }
    @Override protected void onActivityResult(int request,int result,Intent intent){
        super.onActivityResult(request,result,intent);
        if(request!=PICK_ROM && request!=PICK_FOLDER)return;
        if(result!=RESULT_OK||intent==null||intent.getData()==null){pickerBusy=false;if(request==PICK_ROM)chooseRom();else if(!gameActive)finish();return;}
        if(request==PICK_FOLDER){
            new Thread(()->{
                try{storage.select(intent.getData(),intent.getFlags());runOnUiThread(this::chooseRom);}
                catch(Exception error){showError(error);}
            },"NesDQ-folder").start();return;
        }
        importRom(intent.getData());
    }
    private void importRom(Uri uri){
        new Thread(()->{
            File rom=null;
            File romFolder=null;
            try{
                String name=null;
                try(android.database.Cursor cursor=getContentResolver().query(uri,new String[]{android.provider.OpenableColumns.DISPLAY_NAME},null,null,null)){
                    if(cursor!=null && cursor.moveToFirst())name=cursor.getString(0);
                }
                if(name==null || name.isEmpty() || name.equals(".") || name.equals("..") || name.contains("/") || name.contains("\\"))
                    throw new IOException("ROMのファイル名を取得できません");
                romFolder=File.createTempFile("nesdq-rom-","",getCacheDir());
                if(!romFolder.delete() || !romFolder.mkdir())throw new IOException("ROMの一時フォルダーを作成できません");
                rom=new File(romFolder,name);
                try(InputStream in=getContentResolver().openInputStream(uri);OutputStream out=new FileOutputStream(rom)){
                    if(in==null)throw new IOException("ROMを開けません");byte[] buffer=new byte[65536];int n;long total=0;
                    while((n=in.read(buffer))!=-1){total+=n;if(total>16*1024*1024)throw new IOException("ROMが大きすぎます");out.write(buffer,0,n);}
                }
                gameActive=true;openRom(rom.getAbsolutePath().getBytes(java.nio.charset.StandardCharsets.UTF_8));
                rom=null;
            }catch(Exception error){if(rom!=null)rom.delete();if(romFolder!=null)romFolder.delete();showError(error);}
            finally{runOnUiThread(()->pickerBusy=false);}
        },"NesDQ-ROM").start();
    }
}
