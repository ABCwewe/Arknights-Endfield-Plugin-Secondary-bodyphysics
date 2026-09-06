using System;
using System.IO;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace SecondaryMotion.Manager.Services;

// One-time additive migration for the official Typhoeus profile.  A missing
// DB/preset object is copied from the packaged templates; any existing object
// is user-owned and is left completely untouched.
public static class TyphoeusOfficialMigration {
    const string CharacterId = "chr_0034_typhoea";
    const string MarkerName = ".typhoeus_official_v1";
    static readonly JsonSerializerOptions JsonOptions = new() {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    public static string Apply(string managerDir, string dataRoot) {
        string marker = Path.Combine(dataRoot, "data", MarkerName);
        if (File.Exists(marker)) return "already applied";

        try {
            string dbTemplate = FindTemplate(
                Path.Combine(managerDir, "data", "characters.default.template.json"),
                Path.Combine(managerDir, "data", "characters.default.json"));
            string presetTemplate = FindTemplate(
                Path.Combine(managerDir, "presets", "Default.template.json"),
                Path.Combine(managerDir, "presets", "Default.json"));
            var officialDb = ReadCharacter(dbTemplate);
            var officialPreset = ReadCharacter(presetTemplate);
            if (officialDb == null || officialPreset == null)
                return "official templates missing or invalid";

            string dbPath = Path.Combine(dataRoot, "data", "characters.default.json");
            if (!AddIfMissing(dbPath, officialDb, out bool dbChanged))
                return "database migration failed";

            int presetChanges = 0;
            string presetsDir = Path.Combine(dataRoot, "presets");
            if (!Directory.Exists(presetsDir))
                return "presets directory missing";
            foreach (string path in Directory.GetFiles(presetsDir, "*.json")) {
                string name = Path.GetFileNameWithoutExtension(path);
                if (!PresetService.IsValidPresetName(name)) continue;
                if (!AddIfMissing(path, officialPreset, out bool changed))
                    return "preset migration failed: " + name;
                if (changed) presetChanges++;
            }

            Directory.CreateDirectory(Path.GetDirectoryName(marker)!);
            File.WriteAllText(marker,
                "Official Typhoeus profile installed " +
                DateTime.UtcNow.ToString("O") + Environment.NewLine);
            return "applied: db=" + (dbChanged ? "1" : "0") +
                   " presets=" + presetChanges;
        } catch (Exception ex) {
            return "failed: " + ex.Message;
        }
    }

    static string FindTemplate(string packaged, string development) {
        if (File.Exists(packaged)) return packaged;
        if (File.Exists(development)) return development;
        return "";
    }

    static JsonObject? ReadCharacter(string path) {
        if (path.Length == 0 || !File.Exists(path)) return null;
        var root = JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
        var characters = root?["characters"] as JsonObject;
        return characters?[CharacterId] as JsonObject;
    }

    static bool AddIfMissing(string path, JsonObject officialEntry,
                             out bool changed) {
        changed = false;
        if (!File.Exists(path)) return false;
        try {
            var root = JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
            if (root?["characters"] is not JsonObject target) return false;
            if (target[CharacterId] != null) return true;
            target[CharacterId] = officialEntry.DeepClone();
            BackupAndWrite(path, root);
            changed = true;
            return true;
        } catch {
            return false;
        }
    }

    static void BackupAndWrite(string path, JsonObject root) {
        string baseName = path + ".pre_typhoeus_official_v1";
        string backup = baseName;
        for (int i = 2; File.Exists(backup); i++)
            backup = baseName + "." + i;
        File.Copy(path, backup);
        PresetService.AtomicWrite(path,
            root.ToJsonString(JsonOptions) + Environment.NewLine);
    }
}
