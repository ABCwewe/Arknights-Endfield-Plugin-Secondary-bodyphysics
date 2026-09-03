using System;
using System.IO;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace SecondaryMotion.Manager.Services;

// One-shot 3.0.1 data correction only. The 3.0.0 package accidentally shipped
// an intermediate Default Jump table. Replace a character only when its whole
// current Jump object still exactly equals that frozen 3.0.0 value. Any user
// edit, missing/unknown shape, custom character, or unrelated field is kept.
public static class JumpDefaultsV301Hotfix {
    const string MarkerName = ".jump_defaults_v3_0_1_hotfix";
    const string BadTemplateName = "Default.v3.0.0-bad.template.json";
    static readonly JsonSerializerOptions JsonOptions = new() {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    public static string Apply(string managerDir, string dataRoot) {
        string marker = Path.Combine(dataRoot, "data", MarkerName);
        if (File.Exists(marker)) return "already applied";

        try {
            string correctedPath = Path.Combine(
                managerDir, "presets", "Default.template.json");
            if (!File.Exists(correctedPath))
                correctedPath = Path.Combine(
                    managerDir, "presets", "Default.json");
            string badPath = Path.Combine(
                managerDir, "presets", BadTemplateName);
            if (!File.Exists(correctedPath) || !File.Exists(badPath))
                return "comparison templates missing";

            var corrected = ReadCharacters(correctedPath);
            var bad = ReadCharacters(badPath);
            if (corrected == null || bad == null)
                return "comparison templates invalid";

            int changedCharacters = 0;
            int changedPresets = 0;
            int skipped = 0;
            string presetsDir = Path.Combine(dataRoot, "presets");
            if (Directory.Exists(presetsDir)) {
                foreach (string path in Directory.GetFiles(presetsDir, "*.json")) {
                    string name = Path.GetFileNameWithoutExtension(path);
                    if (!PresetService.IsValidPresetName(name)) continue;
                    if (!CorrectPreset(path, bad, corrected, out int changed)) {
                        skipped++;
                        continue;
                    }
                    if (changed > 0) {
                        changedPresets++;
                        changedCharacters += changed;
                    }
                }
            }

            Directory.CreateDirectory(Path.GetDirectoryName(marker)!);
            File.WriteAllText(marker,
                "3.0.1 Default Jump correction applied " +
                DateTime.UtcNow.ToString("O") + Environment.NewLine);
            return "applied: presets=" + changedPresets +
                   " characters=" + changedCharacters +
                   (skipped > 0 ? " skipped=" + skipped : "");
        } catch (Exception ex) {
            return "failed: " + ex.Message;
        }
    }

    static JsonObject? ReadCharacters(string path) {
        var root = JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
        return root?["characters"] as JsonObject;
    }

    static bool CorrectPreset(string path, JsonObject bad,
                              JsonObject corrected, out int changed) {
        changed = 0;
        try {
            var root = JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
            if (root?["characters"] is not JsonObject target) return false;

            foreach (var pair in corrected) {
                if (pair.Value is not JsonObject correctedEntry ||
                    correctedEntry["jump"] is not JsonNode correctedJump ||
                    bad[pair.Key] is not JsonObject badEntry ||
                    badEntry["jump"] is not JsonNode badJump ||
                    target[pair.Key] is not JsonObject targetEntry ||
                    targetEntry["jump"] is not JsonNode currentJump)
                    continue;

                // Five 3.0.0 entries were already correct. Do not rewrite a
                // preset or create a backup when old and new are identical.
                if (JsonNode.DeepEquals(badJump, correctedJump)) continue;
                if (!JsonNode.DeepEquals(currentJump, badJump)) continue;
                targetEntry["jump"] = correctedJump.DeepClone();
                changed++;
            }

            if (changed == 0) return true;
            BackupWithoutOverwrite(path);
            PresetService.AtomicWrite(path,
                root.ToJsonString(JsonOptions) + Environment.NewLine);
            return true;
        } catch {
            return false;
        }
    }

    static void BackupWithoutOverwrite(string path) {
        string basePath = path + ".pre_jump_v3_0_1";
        string backup = basePath;
        for (int suffix = 1; File.Exists(backup); suffix++)
            backup = basePath + "." + suffix;
        File.Copy(path, backup);
    }
}
