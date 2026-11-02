package com.nesdq.app;
import java.io.IOException;
import java.util.*;
import java.util.concurrent.*;
public final class SaveQueueCheck {
 public static void main(String[] args)throws Exception {
  CountDownLatch writing=new CountDownLatch(1),release=new CountDownLatch(1);
  List<Integer> saved=Collections.synchronizedList(new ArrayList<>());
  try(SaveQueue queue=new SaveQueue((path,bytes)->{
   writing.countDown();if(!release.await(3,TimeUnit.SECONDS))throw new IOException("test timeout");saved.add((int)bytes[0]);
  },message->{throw new AssertionError(message);})){
   queue.write("Save/game.sav",new byte[]{1});
   if(!writing.await(1,TimeUnit.SECONDS))throw new AssertionError("writer did not start");
   queue.write("Save/game.sav",new byte[]{2});byte[] latest={3};queue.write("Save/game.sav",latest);latest[0]=9;
   int frames=0;for(int i=0;i<20;i++){++frames;Thread.sleep(5);}
   if(frames!=20 || !saved.isEmpty())throw new AssertionError("foreground was blocked or write completed unexpectedly");
   release.countDown();queue.flush();
   if(!saved.equals(Arrays.asList(1,3)))throw new AssertionError("latest snapshot lost: "+saved);
   System.out.println("PASS background_save=nonblocking snapshots=latest immutable_copy=preserved explicit_flush=complete");
  }finally{release.countDown();}
  CountDownLatch failed=new CountDownLatch(1);
  try(SaveQueue queue=new SaveQueue((path,bytes)->{throw new IOException("simulated write failure");},message->failed.countDown())){
   queue.write("Save/game.sav",new byte[]{1});if(!failed.await(1,TimeUnit.SECONDS))throw new AssertionError("failure not reported");
   boolean rejected=false;try{queue.flush();}catch(IOException expected){rejected=true;}
   if(!rejected || queue.failure()==null)throw new AssertionError("write error was hidden");
   System.out.println("PASS background_save_error=reported");
  }
 }
}
