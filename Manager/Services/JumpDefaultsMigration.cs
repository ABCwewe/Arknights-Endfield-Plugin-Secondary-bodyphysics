using System;
using System.IO;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace SecondaryMotion.Manager.Services;

// One-time v3 migration: install the official Jump table without replacing
// user gait values, bones, names, custom characters, or other preset data.
public static class JumpDefaultsMigration {
    const string MarkerName = ".jump_defaults_v3";
    static readonly JsonSerializerOptions JsonOptions = new() {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    public static string Apply(string managerDir, string dataRoot) {
        string marker = Path.Combine(dataRoot, "data", MarkerName);
        if (File.Exists(marker)) return "already applied";

        try {
            string defaultTemplate = FindTemplate(
                Path.Combine(managerDir, "presets", "Default.template.json"),
                Path.Combine(managerDir, "presets", "Default.json"));
            string dbTemplate = FindTemplate(
                Path.Combine(managerDir, "data", "characters.default.template.json"),
                Path.Combine(managerDir, "data", "characters.default.json"));
            if (defaultTemplate.Length == 0 || dbTemplate.Length == 0)
                return "templates missing";

            var officialPreset = ReadCharacters(defaultTemplate);
            var officialDb = ReadCharacters(dbTemplate);
            if (officialPreset == null || officialDb == null)
                return "invalid templates";

            string dbPath = Path.Combine(dataRoot, "data", "characters.default.json");
            if (!MigrateDatabase(dbPath, officialDb, out int dbCount))
                return "database migration failed";

            int presetCount = 0;
            int skipped = 0;
            string presetsDir = Path.Combine(dataRoot, "presets");
            if (Directory.Exists(presetsDir)) {
                foreach (string path in Directory.GetFiles(presetsDir, "*.json")) {
                    if (MigratePreset(path, officialPreset, officialDb,
                                      out int changed))
                        presetCount += changed;
                    else
                        skipped++;
                }
            }

            Directory.CreateDirectory(Path.GetDirectoryName(marker)!);
            File.WriteAllText(marker,
                "Jump defaults v3 applied " + DateTime.UtcNow.ToString("O") + Environment.NewLine);
            return "applied: db=" + dbCount + " presets=" + presetCount +
                   (skipped > 0 ? " skipped=" + skipped : "");
        } catch (Exception ex) {
            return "failed: " + ex.Message;
        }
    }

    static string FindTemplate(string packaged, string development) {
        if (File.Exists(packaged)) return packaged;
        if (File.Exists(development)) return development;
        return "";
    }

    static JsonObject? ReadCharacters(string path) {
        var root = JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
        return root?["characters"] as JsonObject;
    }

    static JsonNode? OfficialDatabaseJump(JsonObject officialDb, string id) {
        if (officialDb[id] is JsonObject dbEntry &&
            dbEntry["defaults"] is JsonObject defaults && defaults["jump"] != null)
            return defaults["jump"]!.DeepClone();
        return null;
    }

    static bool MigrateDatabase(string path, JsonObject officialDb,
                                out int changed) {
        changed = 0;
        if (!File.Exists(path)) return false;
        try {
            var root = JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
            if (root?["characters"] is not JsonObject target) return false;

            foreach (var pair in officialDb) {
                if (pair.Value is not JsonObject officialEntry) continue;
                if (target[pair.Key] is not JsonObject targetEntry) {
                    target[pair.Key] = officialEntry.DeepClone();
                    targetEntry = (JsonObject)target[pair.Key]!;
                    changed++;
                }
                if (targetEntry["defaults"] is not JsonObject defaults) {
                    defaults = new JsonObject();
                    targetEntry["defaults"] = defaults;
                }
                var jump = OfficialDatabaseJump(officialDb, pair.Key);
                if (jump != null) {
                    defaults["jump"] = jump;
                    changed++;
                }
            }
            BackupAndWrite(path, root);
            return true;
        } catch {
            return false;
        }
    }

    // Missing Jump blocks and untouched technical-DB defaults may receive the
    // official tuned table. Any other existing block is user-owned and must
    // survive the one-time migration.
    static bool PresetJumpNeedsOfficialDefaults(JsonNode? current,
                                                 JsonNode? genericJump) {
        if (current == null) return true;
        return genericJump != null && JsonNode.DeepEquals(current, genericJump);
    }

    static bool MigratePreset(string path, JsonObject officialPreset,
                              JsonObject officialDb,
                              out int changed) {
        changed = 0;
        try {
            var root = JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
            if (root?["characters"] is not JsonObject target) return false;
            foreach (var pair in officialPreset) {
                if (pair.Value is not JsonObject officialEntry ||
                    officialEntry["jump"] == null) continue;
                if (target[pair.Key] is not JsonObject targetEntry) {
                    targetEntry = new JsonObject();
                    target[pair.Key] = targetEntry;
                }
                var genericJump = OfficialDatabaseJump(officialDb, pair.Key);
                if (!PresetJumpNeedsOfficialDefaults(targetEntry["jump"], genericJump))
                    continue;
                targetEntry["jump"] = officialEntry["jump"]!.DeepClone();
                changed++;
            }
            BackupAndWrite(path, root);
            return true;
        } catch {
            return false;
        }
    }

    static void BackupAndWrite(string path, JsonObject root) {
        string backup = path + ".pre_jump_v3";
        if (!File.Exists(backup)) File.Copy(path, backup);
        PresetService.AtomicWrite(path,
            root.ToJsonString(JsonOptions) + Environment.NewLine);
    }
}
