package com.nesdq.app;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Serialized direct writes; only pending snapshots live in memory, never an internal file. */
final class SaveQueue implements AutoCloseable {
    interface Sink {void write(String path,byte[] bytes)throws Exception;}
    interface Failure {void report(String message);}
    private final Sink sink;
    private final Failure failure;
    private final LinkedHashMap<String,byte[]> pending=new LinkedHashMap<>();
    private final ExecutorService executor=Executors.newSingleThreadExecutor(r->new Thread(r,"NesDQ-shared-save"));
    private boolean running;
    private volatile String error;
    SaveQueue(Sink sink,Failure failure){this.sink=sink;this.failure=failure;}
    synchronized void write(String path,byte[] bytes)throws IOException {
        if(error!=null)throw new IOException(error);
        pending.put(path,bytes.clone());
        if(running)return;
        running=true;executor.execute(this::drain);
    }
    private void drain(){
        while(true){
            String path;byte[] snapshot;
            synchronized(this){
                if(pending.isEmpty()){running=false;notifyAll();return;}
                Map.Entry<String,byte[]> first=pending.entrySet().iterator().next();
                path=first.getKey();snapshot=first.getValue();pending.remove(path);
            }
            try{sink.write(path,snapshot);}
            catch(Exception ex){
                synchronized(this){
                    if(!pending.containsKey(path))pending.put(path,snapshot);
                    error="共有フォルダーへの保存に失敗: "+ex.getMessage();
                    running=false;notifyAll();
                }
                failure.report(error);return;
            }
        }
    }
    String failure(){return error;}
    synchronized void flush()throws IOException {
        while(running)try{wait();}catch(InterruptedException ex){Thread.currentThread().interrupt();throw new IOException(ex);}
        if(error!=null)throw new IOException(error);
    }
    @Override public void close(){executor.shutdown();}
}
