package com.nesdq.app;

import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.*;

/** Direct SAF document operations. SAV/state files never use an internal mirror. */
final class SharedFolder {
    private final Context context;
    private Uri tree;
    SharedFolder(Context context) {
        this.context = context;
        String saved = context.getSharedPreferences("storage", 0).getString("tree", null);
        if (saved != null) tree = Uri.parse(saved);
    }
    private Uri root() throws IOException {
        if (tree == null) throw new IOException("NesDQフォルダーを選択してください");
        return DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
    }
    private Uri child(Uri parent, String name) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getDocumentId(parent));
        try (Cursor c = context.getContentResolver().query(children, new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (c == null) throw new IOException("フォルダーを読めません");
            while (c.moveToNext()) if (name.equals(c.getString(1))) return DocumentsContract.buildDocumentUriUsingTree(tree,c.getString(0));
        }
        return null;
    }
    private Uri resolve(String path, boolean create) throws IOException {
        Uri current = root();
        for (String part : path.split("/")) {
            if (part.isEmpty()) continue;
            if (part.equals("..") || part.equals(".")) throw new IOException("不正なパス");
            Uri next = child(current, part);
            if (next == null && create) next = DocumentsContract.createDocument(context.getContentResolver(), current, DocumentsContract.Document.MIME_TYPE_DIR, part);
            if (next == null) { if (!create) return null; throw new IOException("フォルダーを作成できません"); }
            current = next;
        }
        return current;
    }
    synchronized boolean ready() { try { return resolve(prefix()+"Save", false) != null; } catch(Exception e) {return false;} }
    synchronized void select(Uri selected, int flags) throws IOException {
        context.getContentResolver().takePersistableUriPermission(selected, flags & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION));
        Uri previous = tree; String previousBase = prefix();
        try {
            tree = selected;
            Uri selectedRoot = root();
            String name = null;
            try (Cursor c = context.getContentResolver().query(selectedRoot,new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME},null,null,null)) { if(c != null && c.moveToFirst()) name=c.getString(0); }
            if (!"NesDQ".equals(name)) {
                Uri wiiz = child(selectedRoot,"NesDQ");
                if(wiiz == null) wiiz = DocumentsContract.createDocument(context.getContentResolver(),selectedRoot,DocumentsContract.Document.MIME_TYPE_DIR,"NesDQ");
                if(wiiz == null) throw new IOException("NesDQを作成できません");
                // Keep the granted tree URI and remember NesDQ as the relative root.
                base = "NesDQ/";
            } else base = "";
            for(String dir:new String[]{"Rom","Save","state"}) resolve(base+dir,true);
            context.getSharedPreferences("storage",0).edit().putString("tree",tree.toString()).putString("base",base).apply();
        } catch(Exception e) {tree=previous;base=previousBase;throw new IOException(e);}
    }
    private String base = null;
    private String prefix() { if(base==null)base=context.getSharedPreferences("storage",0).getString("base","");return base; }
    synchronized boolean exists(String path) throws IOException {return resolve(prefix()+path,false)!=null;}
    synchronized void mkdir(String path) throws IOException {resolve(prefix()+path,true);}
    synchronized byte[] read(String path, int limit) throws IOException {
        Uri uri=resolve(prefix()+path,false);if(uri==null)throw new IOException("ファイルがありません: "+path);
        try(InputStream in=context.getContentResolver().openInputStream(uri);ByteArrayOutputStream out=new ByteArrayOutputStream()) {
            if(in==null)throw new IOException("読込不可");byte[] buffer=new byte[8192];int n;
            while((n=in.read(buffer))!=-1){if(out.size()+n>limit)throw new IOException("ファイルが大きすぎます");out.write(buffer,0,n);}return out.toByteArray();
        }
    }
    synchronized void write(String path, byte[] bytes) throws IOException {
        int split=path.lastIndexOf('/');String name=path.substring(split+1);
        Uri parent=resolve(prefix()+(split<0?"":path.substring(0,split)),true);
        String token=Long.toString(System.nanoTime());
        Uri temporary=DocumentsContract.createDocument(context.getContentResolver(),parent,"application/octet-stream",name+".tmp-"+token);
        if(temporary==null)throw new IOException("一時ファイル作成不可");
        Uri old=null;boolean committed=false;
        try {
            try(OutputStream out=context.getContentResolver().openOutputStream(temporary,"wt")){if(out==null)throw new IOException("書込不可");out.write(bytes);out.flush();}
            Uri target=child(parent,name);
            if(target!=null){old=DocumentsContract.renameDocument(context.getContentResolver(),target,name+".previous-"+token);if(old==null)throw new IOException("既存ファイルを保護できません");}
            Uri result=DocumentsContract.renameDocument(context.getContentResolver(),temporary,name);
            if(result==null)throw new IOException("保存の確定に失敗");
            committed=true;
        } finally {
            if(!committed){if(old!=null)DocumentsContract.renameDocument(context.getContentResolver(),old,name);DocumentsContract.deleteDocument(context.getContentResolver(),temporary);}
            else if(old!=null)DocumentsContract.deleteDocument(context.getContentResolver(),old);
        }
    }
    synchronized Uri romFolder() throws IOException {
        Uri folder=resolve(prefix()+"Rom",true);
        if(folder==null)throw new IOException("ROMフォルダーがありません");
        return folder;
    }
    synchronized java.util.List<RomEntry> roms() throws IOException {
        Uri folder=romFolder();
        Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(tree,DocumentsContract.getDocumentId(folder));
        java.util.List<RomEntry> entries=new java.util.ArrayList<>();
        try(Cursor c=context.getContentResolver().query(children,new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,DocumentsContract.Document.COLUMN_MIME_TYPE},null,null,null)){
            if(c==null)throw new IOException("ROM一覧を読めません");
            while(c.moveToNext()){
                String name=c.getString(1);
                if(name!=null && name.toLowerCase(java.util.Locale.ROOT).endsWith(".nes") &&
                        !DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2)))
                    entries.add(new RomEntry(name,DocumentsContract.buildDocumentUriUsingTree(tree,c.getString(0))));
            }
        }
        java.util.Collections.sort(entries,(a,b)->a.name.compareToIgnoreCase(b.name));
        return entries;
    }
    static final class RomEntry {
        final String name;final Uri uri;
        RomEntry(String name,Uri uri){this.name=name;this.uri=uri;}
    }
    synchronized void cacheRoms(File cache) throws IOException {
        if(!cache.isDirectory()&&!cache.mkdirs())throw new IOException("ROMキャッシュ作成不可");
        File[] stale=cache.listFiles();if(stale!=null)for(File file:stale)if(file.isFile()&&file.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".nes"))file.delete();
        Uri folder=resolve(prefix()+"Rom",false);if(folder==null)return;
        Uri list=DocumentsContract.buildChildDocumentsUriUsingTree(tree,DocumentsContract.getDocumentId(folder));
        try(Cursor c=context.getContentResolver().query(list,new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME},null,null,null)) {
            if(c==null)throw new IOException("ROM一覧を読めません");
            while(c.moveToNext()){String name=c.getString(0);if(name.contains("/")||!name.toLowerCase(java.util.Locale.ROOT).endsWith(".nes"))continue;
                byte[] bytes=read("Rom/"+name,4*1024*1024);try(FileOutputStream out=new FileOutputStream(new File(cache,name))){out.write(bytes);}
            }
        }
    }
}
