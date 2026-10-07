package org.xuanyuan.originalhd;

import android.app.Activity;
import android.os.Bundle;
import android.os.SystemClock;
import android.graphics.*;
import android.view.*;
import android.content.res.AssetManager;
import android.util.Log;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ConcurrentLinkedQueue;

public class MainActivity extends Activity {
    static { System.loadLibrary("xuanyuan"); }
    static native String nativeCreate(AssetManager assets, String saveDirectory);
    static native void nativeTexture(int pack, int id, int width, int height, int[] pixels);
    static native void nativeFont(byte[] alpha, int[] codes);
    static native String nativeBoot();
    static native String nativeTick(int elapsedMs, int[] pixels);
    static native void nativeKey(int code, boolean pressed);
    static native String nativeStats();
    static native int nativeAudio();
    GameAudio audio;
    boolean soundEnabled;
    volatile boolean running=true,paused=false;
    volatile String message="正在载入原版游戏与高清贴图…";
    GameView gameView;
    final ConcurrentLinkedQueue<int[]> inputs=new ConcurrentLinkedQueue<>();
    final java.util.HashSet<Integer> hardwarePressed=new java.util.HashSet<>();
    Thread worker;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        soundEnabled=getPreferences(MODE_PRIVATE).getBoolean("sound-enabled",true);
        audio=new GameAudio(this,soundEnabled);setVolumeControlStream(android.media.AudioManager.STREAM_MUSIC);
        gameView=new GameView();setContentView(gameView);
        worker=new Thread(this::runGame,"Original ARM game");worker.start();
    }
    byte[] readAsset(String name) throws IOException {
        try(InputStream input=getAssets().open(name);ByteArrayOutputStream output=new ByteArrayOutputStream()) {
            byte[] buffer=new byte[65536];int n;while((n=input.read(buffer))>=0)output.write(buffer,0,n);return output.toByteArray();
        }
    }
    void runGame() {
        try {
            String error=nativeCreate(getAssets(),getFilesDir().getAbsolutePath());
            if(!error.isEmpty())throw new IOException(error);
            JSONObject manifest=new JSONObject(new String(readAsset("hd/manifest.json"),StandardCharsets.UTF_8));
            JSONArray textures=manifest.getJSONArray("textures");
            for(int i=0;i<textures.length();i++) {
                JSONObject t=textures.getJSONObject(i);String pack=t.getString("pack");
                try(InputStream image=getAssets().open("hd/"+t.getString("file"))) {
                    Bitmap b=BitmapFactory.decodeStream(image);if(b==null)throw new IOException("Cannot decode "+t.getString("file"));
                    int[] pixels=new int[b.getWidth()*b.getHeight()];b.getPixels(pixels,0,b.getWidth(),0,0,b.getWidth(),b.getHeight());
                    nativeTexture(pack.equals("map.hkm")?0:pack.equals("mapb.hkm")?1:2,t.getInt("id"),b.getWidth(),b.getHeight(),pixels);b.recycle();
                }
                if(i%40==0){message="正在载入高清贴图 "+i+" / "+textures.length();gameView.postInvalidate();}
            }
            for(String name:new String[]{"arena","command-25112","command-25216","command-29289","command-25910","command-21830","command-30772"}) {
                try(InputStream input=getAssets().open("hd/presentation/"+name+".png")){
                    Bitmap b=BitmapFactory.decodeStream(input);int[] p=new int[b.getWidth()*b.getHeight()];b.getPixels(p,0,b.getWidth(),0,0,b.getWidth(),b.getHeight());nativeTexture(3,name.equals("arena")?0:Integer.parseInt(name.substring(8)),b.getWidth(),b.getHeight(),p);b.recycle();
                }
            }
            JSONArray font=new JSONArray(new String(readAsset("hd/font-index.json"),StandardCharsets.UTF_8));
            int[] chars=new int[font.length()];for(int i=0;i<chars.length;i++)chars[i]=font.getJSONObject(i).getInt("code");nativeFont(readAsset("hd/font.alpha"),chars);
            error=nativeBoot();if(!error.isEmpty())throw new IOException(error);
            message="";
            int[] frame=new int[1440*1920];long previous=SystemClock.elapsedRealtime(),lastLog=previous;
            boolean traceKeys=new File(getFilesDir(),"input-trace.flag").exists();
            while(running) {
                if(paused){SystemClock.sleep(100);previous=SystemClock.elapsedRealtime();continue;}
                int[] key;while((key=inputs.poll())!=null){if(traceKeys)Log.i("XuanyuanInput",String.format("code=0x%x pressed=%d",key[0],key[1]));nativeKey(key[0],key[1]!=0);if(key[1]!=0)audio.input(key[0]);}
                long now=SystemClock.elapsedRealtime();int elapsed=(int)Math.min(500,now-previous);previous=now;
                error=nativeTick(elapsed,frame);if(!error.isEmpty())throw new IOException(error);
                int audioPacket=nativeAudio();
                if((audioPacket&0x01000000)!=0){running=false;runOnUiThread(this::finish);break;}
                audio.update(audioPacket);
                synchronized(gameView.frame) {gameView.frame.setPixels(frame,0,1440,0,0,1440,1920);}
                gameView.postInvalidate();
                if(now-lastLog>10000){Log.i("XuanyuanOriginal",nativeStats());lastLog=now;}
                SystemClock.sleep(30);
            }
        } catch(Throwable error) {
            message="原版运行遇到未兼容功能：\n"+error.getMessage();Log.e("XuanyuanOriginal",message,error);gameView.postInvalidate();
        }
    }
    @Override protected void onPause(){super.onPause();paused=true;if(audio!=null)audio.foreground(false);if(gameView!=null)gameView.releaseTouches();for(int key:hardwarePressed)inputs.add(new int[]{key,0});hardwarePressed.clear();}
    @Override protected void onResume(){super.onResume();paused=false;if(audio!=null)audio.foreground(true);}
    @Override protected void onDestroy(){running=false;if(audio!=null)audio.close();super.onDestroy();}
    @Override public void onBackPressed(){inputs.add(new int[]{0xe037,1});inputs.add(new int[]{0xe037,0});}
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        int k=hardwareKey(event.getKeyCode());
        if(k!=0){
            if(event.getAction()==KeyEvent.ACTION_DOWN){hardwarePressed.add(k);inputs.add(new int[]{k,1});return true;}
            if(event.getAction()==KeyEvent.ACTION_UP){hardwarePressed.remove(k);inputs.add(new int[]{k,0});return true;}
        }
        return super.dispatchKeyEvent(event);
    }
    int hardwareKey(int code){switch(code){case KeyEvent.KEYCODE_DPAD_LEFT:return 0xe033;case KeyEvent.KEYCODE_DPAD_RIGHT:return 0xe034;case KeyEvent.KEYCODE_DPAD_UP:return 0xe031;case KeyEvent.KEYCODE_DPAD_DOWN:return 0xe032;case KeyEvent.KEYCODE_ENTER:case KeyEvent.KEYCODE_DPAD_CENTER:return 0xe035;case KeyEvent.KEYCODE_BACK:return 0xe037;case KeyEvent.KEYCODE_0:return 0xe021;default:if(code>=KeyEvent.KEYCODE_1&&code<=KeyEvent.KEYCODE_9)return 0xe022+code-KeyEvent.KEYCODE_1;return 0;}}

    class GameView extends View {
        final Bitmap frame=Bitmap.createBitmap(1440,1920,Bitmap.Config.ARGB_8888);
        final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
        final RectF game=new RectF();final RectF[] buttons=new RectF[20];
        final int[] codes={0xe031,0xe033,0xe032,0xe034,0xe035,0xe037,0xe022,0xe02a,0,
                           0xe021,0xe022,0xe023,0xe024,0xe025,0xe026,0xe027,0xe028,0xe029,0xe02a,0};
        final String[] labels={"↑","←","↓","→","确定","返回","菜单","炼妖","数字键",
                               "0","1","2","3","4","5","6","7","8","9","声音"};
        boolean numeric=getPreferences(MODE_PRIVATE).getBoolean("numeric-keypad",false);
        final java.util.HashMap<Integer,Integer> touches=new java.util.HashMap<>();
        GameView(){super(MainActivity.this);setBackgroundColor(0xff0c1320);for(int i=0;i<buttons.length;i++)buttons[i]=new RectF();setFocusable(true);}
        @Override protected void onSizeChanged(int w,int h,int oldw,int oldh){layoutControls(w,h);}
        void layoutControls(int w,int h){
            for(RectF button:buttons)button.setEmpty();
            labels[19]=soundEnabled?"声音：开":"声音：关";
            labels[8]=numeric?"方向键":"数字键";
            float bottom=h-(numeric?Math.min(h*.34f,w*.85f):Math.min(h*.24f,w*.55f)),gameH=Math.min(bottom,w*4f/3f),gameW=gameH*.75f;
            game.set((w-gameW)/2,(bottom-gameH)/2,(w+gameW)/2,(bottom+gameH)/2);
            buttons[19].set(w*.815f,Math.max(4,game.top-w*.05f),w*.975f,Math.max(4,game.top-w*.05f)+w*.042f);
            if(numeric){
                float inset=w*.035f,gap=w*.012f,header=Math.min((h-bottom)*.15f,w*.085f);
                float top=bottom+header+inset,cellW=(w*.59f-inset-2*gap)/3f,cellH=(h-inset-top-3*gap)/4f;
                for(int digit=1;digit<=9;digit++){int row=(digit-1)/3,col=(digit-1)%3;float x=inset+col*(cellW+gap),y=top+row*(cellH+gap);buttons[9+digit].set(x,y,x+cellW,y+cellH);}
                float last=top+3*(cellH+gap),zero=inset+cellW+gap;
                buttons[9].set(zero,last,zero+cellW,last+cellH);
                buttons[6].set(inset,last,inset+cellW,last+cellH);
                buttons[7].set(inset+2*(cellW+gap),last,inset+3*cellW+2*gap,last+cellH);
                buttons[8].set(w*.64f,bottom+inset*.25f,w-inset,bottom+header);
                buttons[4].set(w*.64f,top,w-inset,top+2*cellH+gap);
                buttons[5].set(w*.64f,top+2*(cellH+gap),w-inset,h-inset);
                return;
            }
            float unit=Math.min(w*.15f,(h-bottom)*.38f),gap=unit*.15f,left=w*.07f,top=bottom+(h-bottom-unit*2-gap)/2;
            buttons[0].set(left+unit+gap,top,left+2*unit+gap,top+unit);
            buttons[1].set(left,top+unit+gap,left+unit,top+2*unit+gap);
            buttons[2].set(left+unit+gap,top+unit+gap,left+2*unit+gap,top+2*unit+gap);
            buttons[3].set(left+2*(unit+gap),top+unit+gap,left+3*unit+2*gap,top+2*unit+gap);
            buttons[4].set(w*.63f,top,w*.94f,top+unit*1.25f);
            buttons[5].set(w*.63f,top+unit*1.4f,w*.94f,top+2*unit+gap);
            buttons[6].set(left,top,left+unit*.6f,top+unit*.7f);
            buttons[7].set(left+2.4f*unit,top,left+3*unit,top+unit*.7f);
            buttons[8].set(w*.63f,bottom+12,w*.94f,top-12);
        }
        @Override protected void onDraw(Canvas c){
            c.drawColor(0xff0c1320);synchronized(frame){c.drawBitmap(frame,null,game,paint);}
            for(int i=0;i<buttons.length;i++){
                if(buttons[i].isEmpty())continue;
                paint.setColor(touches.containsValue(i)?0xff576b91:0xff25344f);c.drawRoundRect(buttons[i],18,18,paint);
                paint.setColor(0xffecdfb6);paint.setTextSize(Math.min(buttons[i].height()*(i==8||i==19?.60f:i>=9?.55f:.38f),getWidth()*(i==8||i==19?.03f:.08f)));paint.setTextAlign(Paint.Align.CENTER);
                c.drawText(labels[i],buttons[i].centerX(),buttons[i].centerY()-(paint.ascent()+paint.descent())/2,paint);
            }
            if(!message.isEmpty()){
                paint.setColor(0xe6172031);c.drawRect(game,paint);paint.setColor(0xfff4e7c8);paint.setTextSize(getWidth()*.032f);paint.setTextAlign(Paint.Align.LEFT);
                float y=game.centerY();for(String line:message.split("\n")){int step=Math.max(15,(int)(game.width()/paint.getTextSize()));for(int start=0;start<line.length();start+=step){c.drawText(line.substring(start,Math.min(line.length(),start+step)),game.left+20,y,paint);y+=paint.getTextSize()*1.4f;}}
            }
        }
        int hit(float x,float y){for(int i=0;i<buttons.length;i++)if(buttons[i].contains(x,y))return i;return -1;}
        void press(int pointer,int button){Integer old=touches.get(pointer);if(old!=null&&old!=button){inputs.add(new int[]{codes[old],0});touches.remove(pointer);}if(button>=0&&(old==null||old!=button)){touches.put(pointer,button);inputs.add(new int[]{codes[button],1});}}
        void release(int pointer){Integer old=touches.remove(pointer);if(old!=null)inputs.add(new int[]{codes[old],0});}
        void releaseTouches(){for(Integer pointer:new java.util.ArrayList<>(touches.keySet()))release(pointer);}
        @Override public boolean onTouchEvent(MotionEvent e){int action=e.getActionMasked(),index=e.getActionIndex();if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN){int button=hit(e.getX(index),e.getY(index));if(button==19){soundEnabled=!soundEnabled;getPreferences(MODE_PRIVATE).edit().putBoolean("sound-enabled",soundEnabled).apply();audio.setEnabled(soundEnabled);labels[19]=soundEnabled?"声音：开":"声音：关";}else if(button==8){releaseTouches();numeric=!numeric;getPreferences(MODE_PRIVATE).edit().putBoolean("numeric-keypad",numeric).apply();layoutControls(getWidth(),getHeight());}else press(e.getPointerId(index),button);}else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP)release(e.getPointerId(index));else if(action==MotionEvent.ACTION_MOVE){for(int i=0;i<e.getPointerCount();i++){int button=hit(e.getX(i),e.getY(i));press(e.getPointerId(i),button==8||button==19?-1:button);}}else if(action==MotionEvent.ACTION_CANCEL)releaseTouches();invalidate();return true;}
    }
}
