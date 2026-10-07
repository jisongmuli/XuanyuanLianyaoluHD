package org.xuanyuan.originalhd;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.*;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import java.util.*;

/** Local original scores; the supplied legacy game has no playable audio. */
final class GameAudio {
    private final Context context;
    private final Handler main=new Handler(Looper.getMainLooper());
    private final AudioManager manager;
    private final AudioAttributes attributes=new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build();
    private final SoundPool sounds;
    private final int[] ids=new int[12];
    private final Set<Integer> loaded=new HashSet<>();
    private final long[] last=new long[12];
    private final String[] effects={"ui","confirm","cancel","step","attack","impact","magic","heal","teleport","capture","reward","victory"};
    private final String[] tracks={"","title","exploration","palace","cave","underwater","battle","boss"};
    private final AudioFocusRequest focusRequest;
    private MediaPlayer music;
    private int scene,playingScene;
    private boolean foreground=true,enabled,focused=false,ready=false,closed=false,focusInterrupted=false;
    private float duck=1;

    GameAudio(Context c,boolean enabled){
        this.context=c;this.enabled=enabled;
        manager=(AudioManager)c.getSystemService(Context.AUDIO_SERVICE);
        sounds=new SoundPool.Builder().setMaxStreams(8).setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build()).build();
        sounds.setOnLoadCompleteListener((p,id,status)->{if(status==0)loaded.add(id);else Log.e("XuanyuanAudio","Effect decode failed id="+id);});
        for(int i=0;i<effects.length;i++)try(AssetFileDescriptor fd=c.getAssets().openFd("hd/audio/"+effects[i]+".wav")){ids[i]=sounds.load(fd,1);}catch(Exception e){Log.e("XuanyuanAudio","Cannot load "+effects[i],e);}
        focusRequest=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN).setAudioAttributes(attributes).setOnAudioFocusChangeListener(change->{
            if(change==AudioManager.AUDIOFOCUS_GAIN){focused=true;focusInterrupted=false;duck=1;resumeMusic();}
            else if(change==AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK){duck=.22f;applyVolume();}
            else {focused=false;focusInterrupted=true;pauseMusic();sounds.autoPause();}
        },main).build();
    }
    void update(int packet){main.post(()->{if(closed)return;int next=(packet>>>16)&255;if(next>0&&next<tracks.length&&next!=scene){scene=next;changeMusic();}int flags=packet&65535;for(int i=0;i<effects.length;i++)if((flags&(1<<i))!=0)effect(i);});}
    void input(int code){main.post(()->effect(code==0xe035?1:code==0xe037?2:0));}
    void setEnabled(boolean value){enabled=value;focusInterrupted=false;if(enabled){changeMusic();resumeMusic();}else{pauseMusic();sounds.autoPause();abandonFocus();}Log.i("XuanyuanAudio","enabled="+enabled);}
    void foreground(boolean value){foreground=value;if(value){focusInterrupted=false;resumeMusic();}else{pauseMusic();sounds.autoPause();abandonFocus();}}
    private boolean requestFocus(){if(focused)return true;if(focusInterrupted)return false;focused=manager.requestAudioFocus(focusRequest)==AudioManager.AUDIOFOCUS_REQUEST_GRANTED;return focused;}
    private void abandonFocus(){manager.abandonAudioFocusRequest(focusRequest);focused=false;}
    private void applyVolume(){if(music!=null&&ready)music.setVolume(.62f*duck,.62f*duck);}
    private void pauseMusic(){if(music!=null&&ready&&music.isPlaying())music.pause();}
    private void resumeMusic(){if(closed||!foreground||!enabled||!requestFocus())return;sounds.autoResume();if(music!=null&&ready){applyVolume();music.start();}}
    private void changeMusic(){
        if(scene==0||closed)return;
        if(scene==playingScene&&music!=null){resumeMusic();return;}
        if(music!=null){music.release();music=null;}ready=false;playingScene=scene;
        try(AssetFileDescriptor fd=context.getAssets().openFd("hd/audio/"+tracks[scene]+".ogg")){
            final MediaPlayer player=new MediaPlayer();music=player;player.setAudioAttributes(attributes);player.setDataSource(fd.getFileDescriptor(),fd.getStartOffset(),fd.getLength());player.setLooping(true);
            player.setOnPreparedListener(p->{if(closed||music!=p)return;ready=true;Log.i("XuanyuanAudio","music prepared="+tracks[playingScene]+" duration="+p.getDuration()+" loop="+p.isLooping());resumeMusic();});
            player.setOnErrorListener((p,what,extra)->{Log.e("XuanyuanAudio","Music playback error="+what+","+extra);return true;});
            player.prepareAsync();
        }catch(Exception e){Log.e("XuanyuanAudio","Cannot load music "+tracks[scene],e);}
    }
    private void effect(int i){
        if(closed||!enabled||!foreground||!loaded.contains(ids[i])||!requestFocus())return;
        long now=SystemClock.elapsedRealtime();long interval=i==3?170:i<=2?75:180;if(now-last[i]<interval)return;last[i]=now;
        int stream=sounds.play(ids[i],(i==3?.20f:.80f)*duck,(i==3?.20f:.80f)*duck,1,0,1f);
        if(stream!=0)Log.d("XuanyuanAudio","effect="+effects[i]);
    }
    void close(){closed=true;main.removeCallbacksAndMessages(null);if(music!=null)music.release();music=null;sounds.release();abandonFocus();}
}
