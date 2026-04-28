package com.aauto.app;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Typeface;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;
import android.view.Gravity;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

/**
 * Home screen showing available devices for Android Auto.
 *
 * Three equal-width columns (1:1:1):
 *   - Left   : device list. Short-press = ACTIVE (full AA screen).
 *              Long-press = BACKGROUND (audio-only).
 *   - Middle : media playback card for the BACKGROUND session
 *              (album cover, song info, progress bar, controls).
 *   - Right  : track list panel — placeholder for future feature.
 *
 * Toolbar:
 *   - Title shows app version (versionName from manifest) so it's
 *     obvious whether a fresh build was actually installed.
 *   - Overflow menu (3-dot) holds the rarely-used Bluetooth and WiFi
 *     AP toggles — they're sticky settings.
 */
public class DeviceListActivity extends Activity
        implements AaService.DeviceStateListener {
    private static final String TAG = "AA.DeviceList";

    // ===== Design tokens =====
    private static final int COLOR_BG       = 0xFF1A1A1A;
    private static final int COLOR_PANEL    = 0xFF252525;
    private static final int COLOR_TEXT     = 0xFFE0E0E0;
    private static final int COLOR_TEXT_SUB = 0xFF9E9E9E;
    private static final int COLOR_DIVIDER  = 0xFF333333;

    // Per-state status colors.
    private static final int COLOR_STATE_CONNECTING = 0xFF9E9E9E; // gray
    private static final int COLOR_STATE_CONNECTED  = 0xFF2196F3; // blue
    private static final int COLOR_STATE_BACKGROUND = 0xFFFF9800; // orange
    private static final int COLOR_STATE_ACTIVE     = 0xFF4CAF50; // green

    // Text sizes (sp).
    private static final int TEXT_DEVICE_NAME     = 22;
    private static final int TEXT_DEVICE_STATUS   = 18;
    private static final int TEXT_MEDIA_TITLE     = 24;
    private static final int TEXT_MEDIA_SECONDARY = 18;
    private static final int TEXT_MEDIA_HINT      = 16;
    private static final int TEXT_BUTTON          = 22;

    private static final int PAD        = 24;
    private static final int ITEM_PAD_V = 22;
    private static final int ITEM_PAD_H = 24;

    // Album cover size in px (square).
    private static final int ALBUM_COVER_PX = 180;
    // ProgressBar height — horizontal style defaults to ~5dp (barely
    // visible). Make it explicit so the playhead is actually noticeable.
    private static final int PROGRESS_BAR_HEIGHT_PX = 16;

    // Menu item ids.
    private static final int MENU_BLUETOOTH = 1;
    private static final int MENU_WIFI_AP   = 2;

    // Media key codes (subset of aap_protobuf KeyCode.proto).
    private static final int KEYCODE_MEDIA_PLAY_PAUSE = 85;
    private static final int KEYCODE_MEDIA_NEXT       = 87;
    private static final int KEYCODE_MEDIA_PREVIOUS   = 88;

    // ===== Fields =====
    private DeviceAdapter adapter;
    private AaService aaService;

    // Middle-panel media card views (kept to update without rebuilding).
    private ImageView mediaAlbumCover;
    private TextView  mediaStateView;
    private TextView  mediaSongView;
    private TextView  mediaArtistView;
    private TextView  mediaAlbumView;
    private TextView  mediaPlaylistView;
    private TextView  mediaSourceView;
    private ProgressBar mediaProgressBar;
    private TextView  mediaProgressView;
    private TextView  mediaModeView;     // shuffle / repeat icons
    private TextView  mediaHintView;
    private Button    btnPrev;
    private Button    btnPlayPause;
    private Button    btnNext;

    // ===== Lifecycle =====

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setTitle("Android Auto v" + readVersionName());

        // Root: 3 equal-width columns.
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.HORIZONTAL);
        root.setBackgroundColor(COLOR_BG);
        root.setPadding(PAD, PAD, PAD, PAD);

        ListView listView = buildDeviceList();
        LinearLayout.LayoutParams listLp = equalColumn();
        root.addView(listView, listLp);

        LinearLayout mediaPanel = buildMediaPanel();
        LinearLayout.LayoutParams mediaLp = equalColumn();
        mediaLp.leftMargin = PAD;
        root.addView(mediaPanel, mediaLp);

        LinearLayout trackPanel = buildTrackListPanel();
        LinearLayout.LayoutParams trackLp = equalColumn();
        trackLp.leftMargin = PAD;
        root.addView(trackPanel, trackLp);

        setContentView(root);

        Intent serviceIntent = new Intent(this, AaService.class);
        bindService(serviceIntent, serviceConnection, BIND_AUTO_CREATE);

        Log.i(TAG, "DeviceListActivity created");
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshAll();
    }

    @Override
    protected void onDestroy() {
        if (aaService != null) {
            aaService.setDeviceStateListener(null);
        }
        unbindService(serviceConnection);
        super.onDestroy();
    }

    private static LinearLayout.LayoutParams equalColumn() {
        return new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.MATCH_PARENT, 1);
    }

    // ===== Toolbar overflow menu (BT / WiFi AP toggle) =====

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        menu.add(0, MENU_BLUETOOTH, 0, "Bluetooth")
                .setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER);
        menu.add(0, MENU_WIFI_AP, 1, "WiFi AP")
                .setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER);
        return true;
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        boolean btOn = aaService != null && aaService.isBluetoothEnabled();
        boolean apOn = aaService != null && aaService.isWifiApEnabled();
        MenuItem btItem = menu.findItem(MENU_BLUETOOTH);
        MenuItem apItem = menu.findItem(MENU_WIFI_AP);
        if (btItem != null) btItem.setTitle("Bluetooth: " + (btOn ? "ON" : "OFF"));
        if (apItem != null) apItem.setTitle("WiFi AP: "  + (apOn ? "ON" : "OFF"));
        return super.onPrepareOptionsMenu(menu);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case MENU_BLUETOOTH: toggleBluetooth(); return true;
            case MENU_WIFI_AP:   toggleWifiAp();    return true;
            default: return super.onOptionsItemSelected(item);
        }
    }

    // ===== AaService.DeviceStateListener =====

    @Override
    public void onDeviceStateChanged() {
        refreshAll();
    }

    // ===== Refresh =====

    private void refreshAll() {
        invalidateOptionsMenu();
        refreshDeviceList();
        refreshMediaCard();
    }

    private void refreshDeviceList() {
        List<DeviceEntry> entries = new ArrayList<>();
        if (aaService != null) {
            for (SessionManager.SessionEntry session
                    : aaService.getSessionManager().getAll()) {
                String name = session.deviceName != null
                        ? session.deviceName : "Connecting...";
                entries.add(new DeviceEntry(
                        session.sessionId, name,
                        session.transportLabel, session.state));
            }
        }
        adapter.setEntries(entries);
    }

    private void refreshMediaCard() {
        if (aaService == null) {
            clearMediaCard("재생 정보 없음");
            return;
        }
        AaService.PlaybackInfo info = aaService.getBackgroundPlaybackInfo();
        if (info == null
                || (info.song.isEmpty() && info.state == 0)) {
            clearMediaCard("재생 정보 없음");
            return;
        }

        // PLAYING gets the only color emphasis (it's the only "active"
        // state). PAUSED / STOPPED are not warnings — keep them neutral.
        String stateLabel;
        int stateColor;
        boolean isPlaying = false;
        switch (info.state) {
            case 2:  stateLabel = "▶ PLAYING"; stateColor = COLOR_STATE_ACTIVE; isPlaying = true; break;
            case 3:  stateLabel = "⏸ PAUSED";  stateColor = COLOR_TEXT;         break;
            case 1:  stateLabel = "■ STOPPED"; stateColor = COLOR_TEXT_SUB;     break;
            default: stateLabel = "";          stateColor = COLOR_TEXT_SUB;     break;
        }
        mediaStateView.setText(stateLabel);
        mediaStateView.setTextColor(stateColor);

        mediaSongView.setText(info.song);
        mediaArtistView.setText(info.artist);
        mediaAlbumView.setText(info.album);

        if (info.playlist != null && !info.playlist.isEmpty()) {
            mediaPlaylistView.setText("📃 " + info.playlist);
            mediaPlaylistView.setVisibility(View.VISIBLE);
        } else {
            mediaPlaylistView.setVisibility(View.GONE);
        }

        if (info.albumArt != null && info.albumArt.length > 0) {
            try {
                Bitmap bm = BitmapFactory.decodeByteArray(
                        info.albumArt, 0, info.albumArt.length);
                mediaAlbumCover.setImageBitmap(bm);
            } catch (Exception e) {
                Log.w(TAG, "album art decode failed", e);
                mediaAlbumCover.setImageBitmap(null);
            }
        } else {
            mediaAlbumCover.setImageBitmap(null);
        }

        if (info.durationSeconds > 0) {
            mediaProgressBar.setMax(info.durationSeconds);
            mediaProgressBar.setProgress(info.playbackSeconds);
            mediaProgressBar.setVisibility(View.VISIBLE);
            mediaProgressView.setText(formatTime(info.playbackSeconds)
                    + " / " + formatTime(info.durationSeconds));
        } else if (info.playbackSeconds > 0) {
            mediaProgressBar.setVisibility(View.INVISIBLE);
            mediaProgressView.setText(formatTime(info.playbackSeconds));
        } else {
            mediaProgressBar.setVisibility(View.INVISIBLE);
            mediaProgressView.setText("");
        }

        StringBuilder modes = new StringBuilder();
        if (info.shuffle)        modes.append("🔀 ");
        if (info.repeatOne)      modes.append("🔂 ");
        else if (info.repeat)    modes.append("🔁 ");
        mediaModeView.setText(modes.toString().trim());

        mediaSourceView.setText(info.mediaSource.isEmpty()
                ? "" : "source: " + info.mediaSource);
        mediaHintView.setText("");

        // Toggle play/pause button label according to current state.
        btnPlayPause.setText(isPlaying ? "⏸" : "▶");

        boolean controlsEnabled = aaService != null;
        btnPrev.setEnabled(controlsEnabled);
        btnPlayPause.setEnabled(controlsEnabled);
        btnNext.setEnabled(controlsEnabled);
    }

    private void clearMediaCard(String hint) {
        mediaAlbumCover.setImageBitmap(null);
        mediaStateView.setText("");
        mediaSongView.setText("");
        mediaArtistView.setText("");
        mediaAlbumView.setText("");
        mediaPlaylistView.setVisibility(View.GONE);
        mediaProgressBar.setVisibility(View.INVISIBLE);
        mediaProgressView.setText("");
        mediaModeView.setText("");
        mediaSourceView.setText("");
        mediaHintView.setText(hint);
        btnPrev.setEnabled(false);
        btnPlayPause.setEnabled(false);
        btnNext.setEnabled(false);
        btnPlayPause.setText("▶");
    }

    private static String formatTime(int seconds) {
        int m = seconds / 60;
        int s = seconds % 60;
        return String.format("%d:%02d", m, s);
    }

    // ===== BT / WiFi AP toggles (overflow menu) =====

    private void toggleBluetooth() {
        BluetoothAdapter bt = BluetoothAdapter.getDefaultAdapter();
        if (bt == null) {
            Log.w(TAG, "no Bluetooth adapter");
            return;
        }
        try {
            if (bt.isEnabled()) {
                Log.i(TAG, "disabling Bluetooth");
                bt.disable();
            } else {
                Log.i(TAG, "enabling Bluetooth");
                bt.enable();
            }
        } catch (Exception e) {
            Log.e(TAG, "toggleBluetooth failed", e);
        }
    }

    @SuppressWarnings("deprecation")
    private void toggleWifiAp() {
        WifiManager wm = (WifiManager) getSystemService(Context.WIFI_SERVICE);
        int state = wm.getWifiApState();
        boolean wantEnabled = (state != WifiManager.WIFI_AP_STATE_ENABLED
                && state != WifiManager.WIFI_AP_STATE_ENABLING);
        Log.i(TAG, "toggling WiFi AP -> " + (wantEnabled ? "ENABLE" : "DISABLE"));

        if (wantEnabled) {
            if (!tryReflect(wm, "startSoftAp",
                    new Class[]{android.net.wifi.WifiConfiguration.class},
                    new Object[]{null})) {
                tryReflect(wm, "setWifiApEnabled",
                        new Class[]{android.net.wifi.WifiConfiguration.class, boolean.class},
                        new Object[]{null, true});
            }
        } else {
            if (!tryReflect(wm, "stopSoftAp", new Class[0], new Object[0])) {
                tryReflect(wm, "setWifiApEnabled",
                        new Class[]{android.net.wifi.WifiConfiguration.class, boolean.class},
                        new Object[]{null, false});
            }
        }
    }

    private boolean tryReflect(Object target, String method,
                               Class<?>[] paramTypes, Object[] args) {
        try {
            java.lang.reflect.Method m = target.getClass()
                    .getMethod(method, paramTypes);
            Object result = m.invoke(target, args);
            Log.i(TAG, method + " -> " + result);
            return true;
        } catch (NoSuchMethodException e) {
            Log.w(TAG, method + " not available");
            return false;
        } catch (Exception e) {
            Throwable cause = e.getCause() != null ? e.getCause() : e;
            Log.e(TAG, method + " failed", cause);
            return false;
        }
    }

    // ===== Helpers =====

    private String readVersionName() {
        try {
            return getPackageManager()
                    .getPackageInfo(getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return "?";
        }
    }

    private static int colorForState(SessionManager.SessionState s) {
        switch (s) {
            case CONNECTING: return COLOR_STATE_CONNECTING;
            case CONNECTED:  return COLOR_STATE_CONNECTED;
            case BACKGROUND: return COLOR_STATE_BACKGROUND;
            case ACTIVE:     return COLOR_STATE_ACTIVE;
        }
        return COLOR_TEXT_SUB;
    }

    // ===== Left panel: device list =====

    private ListView buildDeviceList() {
        ListView listView = new ListView(this);
        listView.setBackgroundColor(COLOR_PANEL);
        listView.setDivider(new android.graphics.drawable.ColorDrawable(COLOR_DIVIDER));
        listView.setDividerHeight(1);
        adapter = new DeviceAdapter();
        listView.setAdapter(adapter);
        listView.setOnItemClickListener((parent, view, position, id) -> {
            if (aaService == null) return;
            DeviceEntry entry = adapter.getEntry(position);
            if (entry == null || entry.sessionId <= 0) return;
            Log.i(TAG, "short-press: activating session " + entry.sessionId);
            aaService.activateSession(entry.sessionId);
        });
        listView.setOnItemLongClickListener((parent, view, position, id) -> {
            if (aaService == null) return false;
            DeviceEntry entry = adapter.getEntry(position);
            if (entry == null || entry.sessionId <= 0) return false;
            Log.i(TAG, "long-press: backgrounding session " + entry.sessionId);
            aaService.activateBackground(entry.sessionId);
            return true;
        });
        return listView;
    }

    // ===== Middle panel: media playback card =====

    private LinearLayout buildMediaPanel() {
        // Outer panel: vertical
        //   header
        //   ┌─ topRow ──────────────────────┐
        //   │ albumCover │ info column      │
        //   └────────────┴──────────────────┘
        //   progress bar
        //   progress text + mode icons
        //   controls row
        //   hint (when no playback)
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(COLOR_PANEL);
        panel.setPadding(PAD, PAD, PAD, PAD);

        TextView header = new TextView(this);
        header.setText("Now Playing");
        header.setTextSize(TEXT_MEDIA_SECONDARY);
        header.setTextColor(COLOR_TEXT_SUB);
        panel.addView(header, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        // ----- Top row: album cover + info column -----
        LinearLayout topRow = new LinearLayout(this);
        topRow.setOrientation(LinearLayout.HORIZONTAL);
        topRow.setGravity(Gravity.TOP);
        LinearLayout.LayoutParams topRowLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        topRowLp.topMargin = PAD;
        panel.addView(topRow, topRowLp);

        // Album cover always occupies its slot so the layout doesn't
        // shuffle when art arrives/leaves. The image inside is hidden
        // when there's no art, but the box stays.
        mediaAlbumCover = new ImageView(this);
        mediaAlbumCover.setScaleType(ImageView.ScaleType.FIT_CENTER);
        mediaAlbumCover.setBackgroundColor(COLOR_BG); // empty placeholder color
        topRow.addView(mediaAlbumCover, new LinearLayout.LayoutParams(
                ALBUM_COVER_PX, ALBUM_COVER_PX));

        // Info column to the right of the cover.
        LinearLayout info = new LinearLayout(this);
        info.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams infoLp = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
        infoLp.leftMargin = PAD;
        topRow.addView(info, infoLp);

        mediaStateView = new TextView(this);
        mediaStateView.setTextSize(TEXT_MEDIA_SECONDARY);
        mediaStateView.setTextColor(COLOR_STATE_ACTIVE);
        mediaStateView.setTypeface(Typeface.DEFAULT_BOLD);
        info.addView(mediaStateView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        mediaSongView = new TextView(this);
        mediaSongView.setTextSize(TEXT_MEDIA_TITLE);
        mediaSongView.setTextColor(COLOR_TEXT);
        mediaSongView.setTypeface(Typeface.DEFAULT_BOLD);
        info.addView(mediaSongView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        mediaArtistView = new TextView(this);
        mediaArtistView.setTextSize(TEXT_MEDIA_SECONDARY);
        mediaArtistView.setTextColor(COLOR_TEXT);
        info.addView(mediaArtistView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        mediaAlbumView = new TextView(this);
        mediaAlbumView.setTextSize(TEXT_MEDIA_SECONDARY);
        mediaAlbumView.setTextColor(COLOR_TEXT_SUB);
        info.addView(mediaAlbumView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        mediaPlaylistView = new TextView(this);
        mediaPlaylistView.setTextSize(TEXT_MEDIA_HINT);
        mediaPlaylistView.setTextColor(COLOR_TEXT_SUB);
        info.addView(mediaPlaylistView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        mediaSourceView = new TextView(this);
        mediaSourceView.setTextSize(TEXT_MEDIA_HINT);
        mediaSourceView.setTextColor(COLOR_TEXT_SUB);
        info.addView(mediaSourceView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        // ----- Progress bar -----
        mediaProgressBar = new ProgressBar(this, null,
                android.R.attr.progressBarStyleHorizontal);
        LinearLayout.LayoutParams progBarLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, PROGRESS_BAR_HEIGHT_PX);
        progBarLp.topMargin = PAD;
        panel.addView(mediaProgressBar, progBarLp);

        // Progress text + mode icons (shuffle/repeat) on the same row.
        LinearLayout progRow = new LinearLayout(this);
        progRow.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams progRowLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        progRowLp.topMargin = PAD / 4;
        panel.addView(progRow, progRowLp);

        mediaProgressView = new TextView(this);
        mediaProgressView.setTextSize(TEXT_MEDIA_HINT);
        mediaProgressView.setTextColor(COLOR_TEXT_SUB);
        progRow.addView(mediaProgressView, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        mediaModeView = new TextView(this);
        mediaModeView.setTextSize(TEXT_MEDIA_SECONDARY);
        mediaModeView.setTextColor(COLOR_TEXT);
        progRow.addView(mediaModeView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        // ----- Control buttons row: ⏮ ▶/⏸ ⏭ -----
        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);
        controls.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams controlsLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        controlsLp.topMargin = PAD;
        panel.addView(controls, controlsLp);

        btnPrev = makeMediaButton("⏮");
        btnPrev.setOnClickListener(v -> {
            if (aaService != null) aaService.sendMediaKey(KEYCODE_MEDIA_PREVIOUS);
        });
        controls.addView(btnPrev, mediaButtonLp());

        btnPlayPause = makeMediaButton("▶");
        btnPlayPause.setOnClickListener(v -> {
            if (aaService != null) aaService.sendMediaKey(KEYCODE_MEDIA_PLAY_PAUSE);
        });
        controls.addView(btnPlayPause, mediaButtonLp());

        btnNext = makeMediaButton("⏭");
        btnNext.setOnClickListener(v -> {
            if (aaService != null) aaService.sendMediaKey(KEYCODE_MEDIA_NEXT);
        });
        controls.addView(btnNext, mediaButtonLp());

        // ----- Hint (only visible when no playback) -----
        mediaHintView = new TextView(this);
        mediaHintView.setTextSize(TEXT_MEDIA_SECONDARY);
        mediaHintView.setTextColor(COLOR_TEXT_SUB);
        mediaHintView.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams hintLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        hintLp.topMargin = PAD;
        panel.addView(mediaHintView, hintLp);

        return panel;
    }

    private Button makeMediaButton(String label) {
        Button btn = new Button(this);
        btn.setText(label);
        btn.setTextSize(TEXT_BUTTON);
        btn.setAllCaps(false);
        btn.setEnabled(false);
        return btn;
    }

    private LinearLayout.LayoutParams mediaButtonLp() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = PAD / 2;
        lp.rightMargin = PAD / 2;
        return lp;
    }

    // ===== Right panel: track list (placeholder) =====

    private LinearLayout buildTrackListPanel() {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(COLOR_PANEL);
        panel.setPadding(PAD, PAD, PAD, PAD);

        TextView header = new TextView(this);
        header.setText("Track List");
        header.setTextSize(TEXT_MEDIA_SECONDARY);
        header.setTextColor(COLOR_TEXT_SUB);
        panel.addView(header);

        TextView placeholder = new TextView(this);
        placeholder.setText("(coming soon)");
        placeholder.setTextSize(TEXT_MEDIA_HINT);
        placeholder.setTextColor(COLOR_TEXT_SUB);
        placeholder.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT);
        lp.topMargin = PAD * 2;
        panel.addView(placeholder, lp);

        return panel;
    }

    // ===== Service connection =====

    private final ServiceConnection serviceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            AaService.LocalBinder localBinder = (AaService.LocalBinder) binder;
            aaService = localBinder.getService();
            aaService.setDeviceStateListener(DeviceListActivity.this);
            Log.i(TAG, "AaService bound");
            refreshAll();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            aaService = null;
        }
    };

    // ===== Device entry =====

    private static class DeviceEntry {
        final int sessionId;
        final String name;
        final String transport;
        final SessionManager.SessionState state;

        DeviceEntry(int sessionId, String name, String transport,
                    SessionManager.SessionState state) {
            this.sessionId = sessionId;
            this.name = name;
            this.transport = transport;
            this.state = state;
        }
    }

    // ===== Adapter — single-row item: name left, transport+state right =====

    private static class DeviceAdapter extends BaseAdapter {
        private final List<DeviceEntry> entries = new ArrayList<>();

        void setEntries(List<DeviceEntry> list) {
            entries.clear();
            entries.addAll(list);
            notifyDataSetChanged();
        }

        DeviceEntry getEntry(int position) {
            return position < entries.size() ? entries.get(position) : null;
        }

        @Override public int getCount() { return entries.size(); }
        @Override public Object getItem(int pos) { return entries.get(pos); }
        @Override public long getItemId(int pos) { return pos; }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            Context ctx = parent.getContext();
            LinearLayout row;
            if (convertView instanceof LinearLayout) {
                row = (LinearLayout) convertView;
            } else {
                row = new LinearLayout(ctx);
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setGravity(Gravity.CENTER_VERTICAL);
                row.setPadding(ITEM_PAD_H, ITEM_PAD_V, ITEM_PAD_H, ITEM_PAD_V);
            }
            row.removeAllViews();

            DeviceEntry entry = entries.get(position);

            TextView nameView = new TextView(ctx);
            nameView.setText(entry.name);
            nameView.setTextSize(TEXT_DEVICE_NAME);
            nameView.setTextColor(COLOR_TEXT);
            nameView.setTypeface(Typeface.DEFAULT_BOLD);
            LinearLayout.LayoutParams nameLp = new LinearLayout.LayoutParams(
                    0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
            row.addView(nameView, nameLp);

            TextView transportView = new TextView(ctx);
            transportView.setText(entry.transport);
            transportView.setTextSize(TEXT_DEVICE_STATUS);
            transportView.setTextColor(COLOR_TEXT_SUB);
            LinearLayout.LayoutParams transportLp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT);
            transportLp.rightMargin = ITEM_PAD_H;
            row.addView(transportView, transportLp);

            TextView stateView = new TextView(ctx);
            stateView.setText(entry.state.name());
            stateView.setTextSize(TEXT_DEVICE_STATUS);
            stateView.setTextColor(colorForState(entry.state));
            stateView.setTypeface(Typeface.DEFAULT_BOLD);
            row.addView(stateView, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT));

            return row;
        }
    }
}
