package me.yun.silk;

import android.content.Intent;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.AutoCompleteTextView;
import android.widget.ScrollView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.textfield.TextInputLayout;
import com.google.android.material.textview.MaterialTextView;

import java.io.File;

import me.yun.silk.app.R;
import me.yun.silk.utils.Conversion;
import me.yun.silk.utils.Permission;
import me.yun.silk.utils.Preference;
import me.yun.silk.utils.UriUtils;

public class MainActivity extends AppCompatActivity {

    private TextInputEditText etInput;
    private TextInputEditText etOutput;
    private AutoCompleteTextView spSampleRate;
    private MaterialTextView tvLog;
    private ScrollView svLog;

    private Preference pref;
    private SilkCodec codec;

    private final String[] sampleRateLabels = {
            "8000 Hz", "10000 Hz", "12000 Hz",
            "16000 Hz", "22050 Hz", "24000 Hz"
    };
    private final int[] sampleRates = {
            8000, 10000, 12000,
            16000, 22050, 24000
    };

    // 选择输入文件
    private final ActivityResultLauncher<Intent> filePickerLauncher = registerForActivityResult(
    new ActivityResultContracts.StartActivityForResult(),
    result -> {
        if (result.getResultCode() == RESULT_OK && result.getData() != null) {
            Uri uri = result.getData().getData();
            if (uri != null) {
                String realPath = UriUtils.getPathFromUri(this, uri);
                if (realPath != null) {
                    etInput.setText(realPath);
                    if (etOutput.getText() == null || etOutput.getText().toString().trim().isEmpty()) {
                        File inFile = new File(realPath);
                        etOutput.setText(inFile.getParent());
                    }
                } else {
                    Toast.makeText(this, "文件路径解析失败", Toast.LENGTH_SHORT).show();
                }
            }
        }
    }
    );

    // 选择输出文件夹
    private final ActivityResultLauncher<Intent> folderPickerLauncher = registerForActivityResult(
    new ActivityResultContracts.StartActivityForResult(),
    result -> {
        if (result.getResultCode() == RESULT_OK && result.getData() != null) {
            Uri treeUri = result.getData().getData();
            if (treeUri != null) {
                String dirPath = UriUtils.getTreePathFromUri(this, treeUri);
                if (dirPath != null) {
                    etOutput.setText(dirPath); // 仅展示文件夹路径
                } else {
                    Toast.makeText(this, "无法解析该文件夹", Toast.LENGTH_SHORT).show();
                }
            }
        }
    }
    );

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        pref = new Preference(this);
        codec = new SilkCodec();

        initViews();
        loadPreferences();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!Permission.hasStoragePermission(this)) {
            Toast.makeText(this, "请授予文件管理权限以便读取和输出文件", Toast.LENGTH_LONG).show();
        }
    }

    private void initViews() {
        etInput = findViewById(R.id.etInput);
        etOutput = findViewById(R.id.etOutput);
        spSampleRate = findViewById(R.id.spSampleRate);
        tvLog = findViewById(R.id.tvLog);
        svLog = findViewById(R.id.svLog);

        TextInputLayout tilInput = findViewById(R.id.tilInput);
        TextInputLayout tilOutput = findViewById(R.id.tilOutput);

        tilInput.setEndIconOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
            intent.setType("audio/*");
            intent.putExtra(Intent.EXTRA_MIME_TYPES, new String
                    []{"audio/*", "application/octet-stream"});
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            filePickerLauncher.launch(Intent.createChooser(intent, "选择音频文件"));
        });

        tilOutput.setEndIconOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            folderPickerLauncher.launch(intent);
        });

        ArrayAdapter<
                String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_dropdown_item_1line, sampleRateLabels);
        spSampleRate.setAdapter(adapter);

        findViewById(R.id.btnSilkToMp3).setOnClickListener(v -> performAction(0));
        findViewById(R.id.btnMp3ToSilk).setOnClickListener(v -> performAction(1));
        findViewById(R.id.btnAnyToSilk).setOnClickListener(v -> performAction(5));
        findViewById(R.id.btnAnyToPcm).setOnClickListener(v -> performAction(6));
        findViewById(R.id.btnClearLog).setOnClickListener(v -> tvLog.setText(""));

        showMsg("欢迎使用 Silk Codec");

        // 监听键盘以隐藏底部日志区
        View rootView = findViewById(R.id.rootView);
        View layoutLogContainer = findViewById(R.id.layoutLogContainer);

        rootView.getViewTreeObserver().addOnGlobalLayoutListener(() -> {
            Rect r = new Rect();
            rootView.getWindowVisibleDisplayFrame(r);
            int screenHeight = rootView.getRootView().getHeight();
            int keypadHeight = screenHeight - r.bottom;

            if (keypadHeight > screenHeight * 0.15) {
                if (layoutLogContainer.getVisibility() != View.GONE) {
                    layoutLogContainer.setVisibility(View.GONE);
                }
            } else {
                if (layoutLogContainer.getVisibility() != View.VISIBLE) {
                    layoutLogContainer.setVisibility(View.VISIBLE);
                }
            }
        });
    }

    private void loadPreferences() {
        etInput.setText(pref.getInputPath());
        etOutput.setText(pref.getOutputPath());
        int lastHzPos = pref.getHzPos(sampleRateLabels.length - 1);
        if (lastHzPos >= sampleRateLabels.length) {
            lastHzPos = sampleRateLabels.length - 1;
        }
        spSampleRate.setText(sampleRateLabels[lastHzPos], false);
    }

    private int getSelectedSampleRate() {
        String selected = spSampleRate.getText() != null ? spSampleRate.getText().toString() : "";
        for (int i = 0; i < sampleRateLabels.length; i++) {
            if (sampleRateLabels[i].equals(selected)) {
                return sampleRates[i];
            }
        }
        return sampleRates[sampleRates.length - 1];
    }

    private void performAction(int type) {
        if (!Permission.hasStoragePermission(this)) {
            Permission.requestStoragePermission(this);
            return;
        }

        String inputPath = etInput.getText() != null ? etInput.getText().toString().trim() : "";
        String outputDir = etOutput.getText() != null ? etOutput.getText().toString().trim() : "";

        if (inputPath.isEmpty() || outputDir.isEmpty()) {
            Toast.makeText(this, "请输入完整路径", Toast.LENGTH_SHORT).show();
            return;
        }

        File inFile = new File(inputPath);
        String name = inFile.getName();
        int dot = name.lastIndexOf('.');
        String baseName = (dot > 0) ? name.substring(0, dot) : name;

        String targetExt = ".silk";
        switch (type) {
            case 0:
                targetExt = ".mp3";
                break; // Silk -> MP3
            case 1:
                targetExt = ".silk";
                break; // MP3 -> Silk
            case 5:
                targetExt = ".silk";
                break; // Any -> Silk
            case 6:
                targetExt = ".pcm";
                break; // Any -> PCM
        }

        String finalOutputPath = outputDir + "/" + baseName + "_out" + targetExt;

        pref.save(inputPath, outputDir, pref.getHzPos(sampleRateLabels.length - 1));
        tvLog.setText("");
        showMsg("开始处理...");
        showMsg("输出文件: " + baseName + "_out" + targetExt);

        Conversion.startTransform(codec, type, inputPath, finalOutputPath,
                getSelectedSampleRate(), this::showMsg);
    }

    private void showMsg(String msg) {
        runOnUiThread(() -> {
            String currentText = tvLog.getText() != null ? tvLog.getText().toString() : "";
            if (!currentText.isEmpty()) {
                currentText += "\n";
            }
            tvLog.setText(currentText + msg);
            svLog.post(() -> svLog.fullScroll(View.FOCUS_DOWN));
        });
    }
}