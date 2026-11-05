using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;

if (args.Length != 2) throw new ArgumentException("Expected original ESP and output ESP paths");
var source = Path.GetFullPath(args[0]);
var destination = Path.GetFullPath(args[1]);
if (StringComparer.OrdinalIgnoreCase.Equals(source, destination)) throw new ArgumentException("Output must be a separate copy");
var mod = SkyrimMod.CreateFromBinary(source, SkyrimRelease.SkyrimSE);
var quest = mod.Quests.Single(q => q.EditorID == "ValhallaCombat_MCM_Quest");
var removed = quest.FormKey;
mod.Quests.Remove(removed);
Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
mod.BeginWrite.ToPath(destination).WithLoadOrderFromHeaderMasters().WithDataFolder(Path.GetDirectoryName(source)!).Write();
var reread = SkyrimMod.CreateFromBinary(destination, SkyrimRelease.SkyrimSE);
if (reread.ModHeader.MasterReferences.Any(m => m.Master.FileName.String.Equals("MCMHelper.esp", StringComparison.OrdinalIgnoreCase) || m.Master.FileName.String.Equals("SkyUI_SE.esp", StringComparison.OrdinalIgnoreCase)))
    throw new InvalidDataException("Menu dependency still referenced");
var expected = mod.EnumerateMajorRecords().ToDictionary(r => r.FormKey);
var actual = reread.EnumerateMajorRecords().ToDictionary(r => r.FormKey);
if (!expected.Keys.ToHashSet().SetEquals(actual.Keys)) throw new InvalidDataException("Gameplay form keys changed");
foreach (var (key, record) in expected)
    if (!record.Equals(actual[key])) throw new InvalidDataException($"Gameplay record changed: {key} {record.EditorID}");
Console.WriteLine($"Removed only menu quest {removed}; {actual.Count} gameplay records and form keys verified after roundtrip");
Console.WriteLine("Masters: " + string.Join(", ", reread.ModHeader.MasterReferences.Select(m => m.Master.FileName)));
