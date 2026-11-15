package com.ocarinastudio;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * First screen: copies the files the game needs to its folder (/storage/emulated/0/com.ocarinastudio), lets the
 * user pick the ROM with the system file picker (no storage permission needed) and then starts the game.
 */
public class LauncherActivity extends Activity {
    private static final int PICK_ROM = 1;
    // Bump to re-copy the bundled files after an update.
    private static final int ASSET_VERSION = 1;

    private File dataDir;
    private TextView status;
    private Button pickButton;
    private Button playButton;
    private ProgressBar progress;
    private Button permissionButton;
    private boolean prepared = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        dataDir = DataDir.dir();
        buildUi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!DataDir.hasAccess(this)) {
            showPermissionStep();
            return;
        }
        if (prepared) {
            return;
        }
        prepared = true;
        permissionButton.setVisibility(View.GONE);
        status.setText("Preparing...");
        new Thread(() -> {
            dataDir.mkdirs();
            try {
                copyBundledFiles();
            } catch (IOException e) {
                runOnUiThread(() -> status.setText("Could not prepare the game files: " + e.getMessage()));
                return;
            }
            runOnUiThread(this::refresh);
        }).start();
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(Color.rgb(24, 26, 32));
        root.setPadding(48, 24, 48, 24);

        TextView title = new TextView(this);
        title.setText("OcarinaStudio");
        title.setTextSize(30);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        status = new TextView(this);
        status.setTextSize(16);
        status.setTextColor(Color.LTGRAY);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, 24, 0, 24);
        status.setText("Preparing...");
        root.addView(status);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setVisibility(View.GONE);
        root.addView(progress, new LinearLayout.LayoutParams(600, LinearLayout.LayoutParams.WRAP_CONTENT));

        permissionButton = new Button(this);
        permissionButton.setText("Give file access");
        permissionButton.setVisibility(View.GONE);
        permissionButton.setOnClickListener(v -> DataDir.requestAccess(this, 2));
        root.addView(permissionButton);

        pickButton = new Button(this);
        pickButton.setText("Select the ROM file");
        pickButton.setVisibility(View.GONE);
        pickButton.setOnClickListener(v -> pickRom());
        root.addView(pickButton);

        playButton = new Button(this);
        playButton.setText("Play");
        playButton.setVisibility(View.GONE);
        playButton.setOnClickListener(v -> startGame());
        root.addView(playButton);

        setContentView(root);
    }

    private void showPermissionStep() {
        pickButton.setVisibility(View.GONE);
        playButton.setVisibility(View.GONE);
        progress.setVisibility(View.GONE);
        permissionButton.setVisibility(View.VISIBLE);
        status.setText("OcarinaStudio keeps its files (ROM, settings, mods, textures, animations) in the folder\n"
                + "/storage/emulated/0/com.ocarinastudio\n"
                + "so you can reach it from any file manager. Tap the button and allow access to all files.");
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        // onResume runs right after and checks the permission again
    }

    private boolean hasGameData() {
        return new File(dataDir, "oot.o2r").exists();
    }

    private boolean hasRom() {
        File[] files = dataDir.listFiles();
        if (files == null) {
            return false;
        }
        for (File f : files) {
            String name = f.getName().toLowerCase();
            if (name.endsWith(".z64") || name.endsWith(".n64") || name.endsWith(".v64")) {
                return true;
            }
        }
        return false;
    }

    private void refresh() {
        progress.setVisibility(View.GONE);
        if (hasGameData()) {
            startGame();
            return;
        }
        playButton.setVisibility(View.GONE);
        pickButton.setVisibility(View.VISIBLE);
        if (hasRom()) {
            status.setText("ROM found. The first start extracts it (a few minutes), then the game opens.");
            playButton.setText("Extract and play");
            playButton.setVisibility(View.VISIBLE);
            pickButton.setText("Select a different ROM");
        } else {
            status.setText("Select your Ocarina of Time ROM (.z64 / .n64 / .v64).\n"
                    + "It is copied to /storage/emulated/0/com.ocarinastudio and never leaves your phone.");
        }
    }

    private void pickRom() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_ROM);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM || resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        final Uri uri = data.getData();
        pickButton.setVisibility(View.GONE);
        playButton.setVisibility(View.GONE);
        status.setText("Copying the ROM...");
        progress.setVisibility(View.VISIBLE);
        progress.setIndeterminate(true);
        new Thread(() -> {
            try {
                // Remove older ROMs so only the selected one is used.
                File[] old = dataDir.listFiles();
                if (old != null) {
                    for (File f : old) {
                        String n = f.getName().toLowerCase();
                        if (n.endsWith(".z64") || n.endsWith(".n64") || n.endsWith(".v64")) {
                            f.delete();
                        }
                    }
                }
                try (InputStream in = getContentResolver().openInputStream(uri);
                     OutputStream out = new FileOutputStream(new File(dataDir, "rom.z64"))) {
                    copy(in, out);
                }
                runOnUiThread(this::refresh);
            } catch (IOException e) {
                runOnUiThread(() -> {
                    status.setText("Could not copy the ROM: " + e.getMessage());
                    progress.setVisibility(View.GONE);
                    pickButton.setVisibility(View.VISIBLE);
                });
            }
        }).start();
    }

    private void startGame() {
        startActivity(new Intent(this, OcarinaStudioActivity.class));
        finish();
    }

    // ---- bundled files (soh.o2r and the extractor's assets folder) ------------------------------

    private void copyBundledFiles() throws IOException {
        SharedPreferences prefs = getSharedPreferences("files", MODE_PRIVATE);
        if (prefs.getInt("assetVersion", 0) == ASSET_VERSION && new File(dataDir, "soh.o2r").exists()) {
            return;
        }
        AssetManager assets = getAssets();
        try (InputStream in = assets.open("soh.o2r");
             OutputStream out = new FileOutputStream(new File(dataDir, "soh.o2r"))) {
            copy(in, out);
        }
        copyAssetDir(assets, "extractor", new File(dataDir, "assets"));
        prefs.edit().putInt("assetVersion", ASSET_VERSION).apply();
    }

    private void copyAssetDir(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            target.getParentFile().mkdirs();
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                copy(in, out);
            }
            return;
        }
        target.mkdirs();
        for (String child : children) {
            copyAssetDir(assets, path + "/" + child, new File(target, child));
        }
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[1 << 16];
        int n;
        while ((n = in.read(buffer)) > 0) {
            out.write(buffer, 0, n);
        }
    }
}
