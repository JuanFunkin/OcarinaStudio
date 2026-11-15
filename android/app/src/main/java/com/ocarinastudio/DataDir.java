package com.ocarinastudio;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.system.Os;

import java.io.File;

/** The game folder: /storage/emulated/0/com.ocarinastudio (needs the "all files" permission). */
final class DataDir {
    private DataDir() {}

    static File dir() {
        return new File(Environment.getExternalStorageDirectory(), "com.ocarinastudio");
    }

    static boolean hasAccess(Context context) {
        if (Build.VERSION.SDK_INT >= 30) {
            return Environment.isExternalStorageManager();
        }
        return context.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
    }

    /** Opens the system screen where the user grants the permission. */
    static void requestAccess(Activity activity, int requestCode) {
        if (Build.VERSION.SDK_INT >= 30) {
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + activity.getPackageName()));
            try {
                activity.startActivity(intent);
            } catch (Exception e) {
                activity.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        } else {
            activity.requestPermissions(new String[] {
                    android.Manifest.permission.WRITE_EXTERNAL_STORAGE,
                    android.Manifest.permission.READ_EXTERNAL_STORAGE }, requestCode);
        }
    }

    /** Tells the native game where its folder is (read by libsoh.so at start). */
    static void exportToNative() {
        try {
            File dir = dir();
            dir.mkdirs();
            Os.setenv("OCARINA_DATA_DIR", dir.getAbsolutePath(), true);
            // The extractor works in std::filesystem::temp_directory_path(), which on Android points to a
            // folder apps cannot write to. Give it one inside the game folder.
            File tmp = new File(dir, "tmp");
            tmp.mkdirs();
            Os.setenv("TMPDIR", tmp.getAbsolutePath(), true);
        } catch (Exception ignored) {
        }
    }
}
