package me.yun.silk.utils;

import android.content.Context;
import android.content.SharedPreferences;

public class Preference {
    private final SharedPreferences pref;

    public Preference(Context context) {
        pref = context.getSharedPreferences("silk_v3_data", Context.MODE_PRIVATE);
    }

    public String getInputPath() { return pref.getString("in", ""); }
    public String getOutputPath() { return pref.getString("out", ""); }
    public int getHzPos(int defaultPos) { return pref.getInt("hz_pos", defaultPos); }

    public void save(String in, String out, int hzPos) {
        pref.edit()
            .putString("in", in)
            .putString("out", out)
            .putInt("hz_pos", hzPos)
            .apply();
    }
}