package com.ocarinastudio;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ClipDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * First screen: copies the files the game needs to its folder (/storage/emulated/0/com.ocarinastudio), lets the
 * user pick the ROM with the system file picker and then starts the game. Every long step shows a progress bar
 * with a percentage.
 */
public class LauncherActivity extends Activity {
    private static final int PICK_ROM = 1;
    // Bump to re-copy the bundled files after an update.
    private static final int ASSET_VERSION = 1;

    private static final int BG_TOP = Color.rgb(12, 18, 30);
    private static final int BG_BOTTOM = Color.rgb(30, 22, 52);
    private static final int CARD = Color.argb(200, 22, 28, 44);
    private static final int ACCENT = Color.rgb(124, 255, 155);
    private static final int ACCENT_DARK = Color.rgb(46, 160, 92);
    private static final int TEXT = Color.rgb(232, 236, 244);
    private static final int TEXT_DIM = Color.rgb(150, 158, 176);

    private File dataDir;
    private TextView status;
    private TextView percent;
    private TextView hint;
    private Button pickButton;
    private Button playButton;
    private Button permissionButton;
    private ProgressBar progress;
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
        showProgress("Preparing the game files...", 0);
        new Thread(() -> {
            dataDir.mkdirs();
            try {
                copyBundledFiles();
            } catch (IOException e) {
                runOnUiThread(() -> {
                    showMessage("Could not prepare the game files:\n" + e.getMessage());
                    prepared = false;
                });
                return;
            }
            runOnUiThread(this::refresh);
        }).start();
    }

    // ---- UI ------------------------------------------------------------------------------------

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private static GradientDrawable rounded(int color, int radiusPx) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(radiusPx);
        return d;
    }

    private Button makeButton(String text, boolean primary) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(17);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setStateListAnimator(null);
        if (primary) {
            GradientDrawable d = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                    new int[] {ACCENT_DARK, ACCENT});
            d.setCornerRadius(dp(28));
            b.setBackground(d);
            b.setTextColor(Color.rgb(8, 30, 18));
        } else {
            GradientDrawable d = rounded(Color.argb(40, 255, 255, 255), dp(28));
            d.setStroke(dp(1), Color.argb(120, 255, 255, 255));
            b.setBackground(d);
            b.setTextColor(TEXT);
        }
        LinearLayout.LayoutParams lp =
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(56));
        lp.topMargin = dp(12);
        b.setLayoutParams(lp);
        b.setVisibility(View.GONE);
        return b;
    }

    private void buildUi() {
        GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[] {BG_TOP, BG_BOTTOM});

        ScrollView scroll = new ScrollView(this);
        scroll.setBackground(bg);
        scroll.setFillViewport(true);

        LinearLayout outer = new LinearLayout(this);
        outer.setOrientation(LinearLayout.VERTICAL);
        outer.setGravity(Gravity.CENTER);
        outer.setPadding(dp(20), dp(16), dp(20), dp(16));
        scroll.addView(outer, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.ic_foreground);
        logo.setBackground(rounded(Color.rgb(27, 58, 42), dp(24)));
        logo.setClipToOutline(true);
        outer.addView(logo, new LinearLayout.LayoutParams(dp(72), dp(72)));

        TextView title = new TextView(this);
        title.setText("OcarinaStudio");
        title.setTextSize(30);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, dp(10), 0, 0);
        outer.addView(title);

        TextView subtitle = new TextView(this);
        subtitle.setText("Machinima studio for Ocarina of Time");
        subtitle.setTextSize(14);
        subtitle.setTextColor(ACCENT);
        subtitle.setGravity(Gravity.CENTER);
        subtitle.setPadding(0, 0, 0, dp(16));
        outer.addView(subtitle);

        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setGravity(Gravity.CENTER_HORIZONTAL);
        card.setBackground(rounded(CARD, dp(24)));
        card.setPadding(dp(22), dp(20), dp(22), dp(22));
        outer.addView(card, new LinearLayout.LayoutParams(dp(460), ViewGroup.LayoutParams.WRAP_CONTENT));

        status = new TextView(this);
        status.setTextSize(16);
        status.setTextColor(TEXT);
        status.setGravity(Gravity.CENTER);
        status.setLineSpacing(0, 1.15f);
        card.addView(status);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        GradientDrawable track = rounded(Color.argb(70, 255, 255, 255), dp(6));
        GradientDrawable fill = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                new int[] {ACCENT_DARK, ACCENT});
        fill.setCornerRadius(dp(6));
        LayerDrawable layers = new LayerDrawable(new android.graphics.drawable.Drawable[] {
                track, new ClipDrawable(fill, Gravity.START, ClipDrawable.HORIZONTAL)});
        layers.setId(0, android.R.id.background);
        layers.setId(1, android.R.id.progress);
        progress.setProgressDrawable(layers);
        progress.setVisibility(View.GONE);
        LinearLayout.LayoutParams plp =
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(12));
        plp.topMargin = dp(16);
        card.addView(progress, plp);

        percent = new TextView(this);
        percent.setTextSize(15);
        percent.setTypeface(Typeface.DEFAULT_BOLD);
        percent.setTextColor(ACCENT);
        percent.setGravity(Gravity.CENTER);
        percent.setPadding(0, dp(6), 0, 0);
        percent.setVisibility(View.GONE);
        card.addView(percent);

        permissionButton = makeButton("Give file access", true);
        permissionButton.setOnClickListener(v -> DataDir.requestAccess(this, 2));
        card.addView(permissionButton);

        playButton = makeButton("Play", true);
        playButton.setOnClickListener(v -> startGame());
        card.addView(playButton);

        pickButton = makeButton("Select the ROM file", true);
        pickButton.setOnClickListener(v -> pickRom());
        card.addView(pickButton);

        hint = new TextView(this);
        hint.setTextSize(12);
        hint.setTextColor(TEXT_DIM);
        hint.setGravity(Gravity.CENTER);
        hint.setPadding(0, dp(14), 0, 0);
        hint.setText("Files folder: /storage/emulated/0/com.ocarinastudio");
        outer.addView(hint);

        setContentView(scroll);
    }

    private void hideButtons() {
        pickButton.setVisibility(View.GONE);
        playButton.setVisibility(View.GONE);
        permissionButton.setVisibility(View.GONE);
    }

    /** fraction < 0 shows an indeterminate bar (unknown length). */
    private void showProgress(String text, int permille) {
        hideButtons();
        status.setText(text);
        progress.setVisibility(View.VISIBLE);
        percent.setVisibility(View.VISIBLE);
        if (permille < 0) {
            progress.setIndeterminate(true);
            percent.setText("");
        } else {
            progress.setIndeterminate(false);
            progress.setProgress(permille);
            percent.setText((permille / 10) + "%");
        }
    }

    private void showMessage(String text) {
        progress.setVisibility(View.GONE);
        percent.setVisibility(View.GONE);
        status.setText(text);
    }

    private void showPermissionStep() {
        hideButtons();
        progress.setVisibility(View.GONE);
        percent.setVisibility(View.GONE);
        permissionButton.setVisibility(View.VISIBLE);
        status.setText("OcarinaStudio keeps its files (ROM, settings, mods, textures and animations) in\n"
                + "/storage/emulated/0/com.ocarinastudio\n"
                + "so you can reach them from any file manager.\n\n"
                + "Tap the button and allow access to all files.");
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        // onResume runs right after and checks the permission again
    }

    // ---- flow ----------------------------------------------------------------------------------

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
        percent.setVisibility(View.GONE);
        if (hasGameData()) {
            startGame();
            return;
        }
        hideButtons();
        pickButton.setVisibility(View.VISIBLE);
        if (hasRom()) {
            status.setText("ROM found.\nThe first start extracts it (a few minutes), then the game opens.");
            playButton.setText("Extract and play");
            playButton.setVisibility(View.VISIBLE);
            pickButton.setText("Select a different ROM");
            pickButton.setBackground(secondaryBackground());
            pickButton.setTextColor(TEXT);
        } else {
            status.setText("Select your Ocarina of Time ROM (.z64 / .n64 / .v64).\n"
                    + "It is copied into the files folder and never leaves your phone.");
            pickButton.setText("Select the ROM file");
        }
    }

    private GradientDrawable secondaryBackground() {
        GradientDrawable d = rounded(Color.argb(40, 255, 255, 255), dp(28));
        d.setStroke(dp(1), Color.argb(120, 255, 255, 255));
        return d;
    }

    private void pickRom() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_ROM);
    }

    private long querySize(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, null, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int i = c.getColumnIndex(OpenableColumns.SIZE);
                if (i >= 0 && !c.isNull(i)) {
                    return c.getLong(i);
                }
            }
        } catch (RuntimeException ignored) {
        }
        return -1;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM || resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        final Uri uri = data.getData();
        final long total = querySize(uri);
        showProgress("Copying the ROM...", total > 0 ? 0 : -1);
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
                    byte[] buffer = new byte[1 << 16];
                    long done = 0;
                    int lastShown = -1;
                    int n;
                    while ((n = in.read(buffer)) > 0) {
                        out.write(buffer, 0, n);
                        done += n;
                        if (total > 0) {
                            int p = (int) (done * 1000 / total);
                            if (p != lastShown) {
                                lastShown = p;
                                final int shown = p;
                                runOnUiThread(() -> showProgress("Copying the ROM...", shown));
                            }
                        }
                    }
                }
                runOnUiThread(this::refresh);
            } catch (IOException e) {
                runOnUiThread(() -> {
                    showMessage("Could not copy the ROM:\n" + e.getMessage());
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

    private long copiedBytes;
    private long totalBytes;
    private int lastPermille = -1;

    private void copyBundledFiles() throws IOException {
        SharedPreferences prefs = getSharedPreferences("files", MODE_PRIVATE);
        if (prefs.getInt("assetVersion", 0) == ASSET_VERSION && new File(dataDir, "soh.o2r").exists()) {
            return;
        }
        AssetManager assets = getAssets();

        // Work out the total size first so the bar is real.
        List<String> files = new ArrayList<>();
        listAssetFiles(assets, "extractor", files);
        totalBytes = assetSize(assets, "soh.o2r");
        for (String f : files) {
            totalBytes += assetSize(assets, f);
        }
        copiedBytes = 0;
        lastPermille = -1;

        copyAssetFile(assets, "soh.o2r", new File(dataDir, "soh.o2r"));
        for (String f : files) {
            copyAssetFile(assets, f, new File(dataDir, "assets/" + f.substring("extractor/".length())));
        }
        prefs.edit().putInt("assetVersion", ASSET_VERSION).apply();
    }

    private void listAssetFiles(AssetManager assets, String path, List<String> out) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            out.add(path);
            return;
        }
        for (String child : children) {
            listAssetFiles(assets, path + "/" + child, out);
        }
    }

    private long assetSize(AssetManager assets, String path) {
        try (InputStream in = assets.open(path)) {
            return in.available();
        } catch (IOException e) {
            return 0;
        }
    }

    private void copyAssetFile(AssetManager assets, String path, File target) throws IOException {
        File parent = target.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }
        try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[1 << 16];
            int n;
            while ((n = in.read(buffer)) > 0) {
                out.write(buffer, 0, n);
                copiedBytes += n;
                if (totalBytes > 0) {
                    int p = (int) Math.min(1000, copiedBytes * 1000 / totalBytes);
                    if (p != lastPermille) {
                        lastPermille = p;
                        final int shown = p;
                        runOnUiThread(() -> showProgress("Preparing the game files...", shown));
                    }
                }
            }
        }
    }
}
