// TODO 2.3: writes rooms, objects, motions, items and missions as JSON, from this PC's OpenKH extraction,
// with OpenKH's own readers (Place, AreaDataScript, Objentry, Bar, Item, Msg). Output stays on this PC (build/ is git-ignored).
// Run from the repo root: dotnet run --project tools/catalog [-- <data\kh2> <obj.md> <out dir>]
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using OpenKh.Kh2;
using OpenKh.Kh2.Ard;
using OpenKh.Kh2.Messages;
using OpenKh.Kh2.SystemData;

var data = args.Length > 0 ? args[0] : @"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS openkh\data\kh2";
var objNames = args.Length > 1 ? args[1] : @"D:\tools\openkh\docs\kh2\dictionary\obj.md";
var outDir = args.Length > 2 ? args[2] : Path.Combine("build", "catalog");
Directory.CreateDirectory(outDir);

Bar ReadBar(string path) { using var fs = File.OpenRead(path); return Bar.Read(fs); }

// Game text: msg/us/sys.bar, ids without the 0x8000 bit (VERIFIED_OFFSETS 4, room names).
var sys = Msg.Read(ReadBar(Path.Combine(data, "msg", "us", "sys.bar")).First(e => e.Type == Bar.EntryType.List).Stream)
    .GroupBy(e => e.Id).ToDictionary(g => g.Key, g => g.First().Data);
string Text(int id) => sys.TryGetValue(id & 0x7FFF, out var d) ? MsgSerializer.SerializeText(Encoders.InternationalSystem.Decode(d)) : null;

// World codes in world-id order (openkh docs/kh2/worlds.md).
string[] worlds = { "zz", "es", "tt", "di", "hb", "bb", "he", "al", "mu", "po", "lk", "lm", "dc", "wi", "nm", "wm", "ca", "tr", "eh" };
Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
var sjis = Encoding.GetEncoding(932);
// Message ids from msg/us/place.bin (the one the game uses; 00place.bin's ids are from development, e.g. tt25),
// Japanese names from 00place.bin (OpenKH docs/kh2/file/type/place.md).
Dictionary<string, List<Place>> ReadPlaces(string path) { using var fs = File.OpenRead(path); return Place.Read(fs); }
var places = ReadPlaces(Path.Combine(data, "msg", "us", "place.bin"));
var devPlaces = ReadPlaces(Path.Combine(data, "00place.bin"));

var rooms = new List<object>();
var missionUse = new SortedDictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
for (var w = 0; w < worlds.Length; w++)
{
    var list = places.GetValueOrDefault(worlds[w]) ?? new();
    var dev = devPlaces.GetValueOrDefault(worlds[w]) ?? new();
    var ardRooms = Directory.GetFiles(Path.Combine(data, "ard", "us"), worlds[w] + "??.ard")
        .Select(f => int.TryParse(Path.GetFileNameWithoutExtension(f)[2..], out var n) ? n + 1 : 0);
    var count = ardRooms.Append(list.Count).Append(dev.Count).Max();
    for (var r = 0; r < count; r++)
    {
        var code = $"{worlds[w]}{r:D2}";
        var ard = Path.Combine(data, "ard", "us", code + ".ard");
        if (r >= list.Count && r >= dev.Count && !File.Exists(ard)) continue;
        var programs = new SortedDictionary<string, SortedDictionary<int, string[]>>();
        if (File.Exists(ard))
            foreach (var e in ReadBar(ard).Where(e => e.Type == Bar.EntryType.AreaDataScript))
            {
                var byId = programs.TryGetValue(e.Name, out var p) ? p : programs[e.Name] = new();
                foreach (var s in AreaDataScript.Read(e.Stream))
                {
                    byId[s.ProgramId] = s.Functions.Select(f => f.ToString()).ToArray();
                    foreach (var m in s.Functions.OfType<AreaDataScript.Mission>())
                        (missionUse.TryGetValue(m.Value, out var u) ? u : missionUse[m.Value] = new()).Add($"{code} {e.Name} {s.ProgramId}");
                }
            }
        rooms.Add(new { world = w, room = r, code, name = r < list.Count ? Text(list[r].MessageId) : null,
                        jp = r < dev.Count ? sjis.GetString(dev[r].Name) : null,
                        has_file = File.Exists(ard), programs });
    }
}

// Objects: the game's 00objentry.bin; English names from OpenKH's dictionary (a lead, not the game's text).
var names = File.ReadLines(objNames).Select(l => l.Split('|')).Where(p => p.Length > 3 && uint.TryParse(p[1].Trim(), out _))
    .GroupBy(p => uint.Parse(p[1].Trim())).ToDictionary(g => g.Key, g => g.First()[3].Trim());
List<Objentry> objs;
using (var fs = File.OpenRead(Path.Combine(data, "00objentry.bin"))) objs = Objentry.Read(fs);
var objects = objs.Select(o => new { id = o.ObjectId, hex = $"{o.ObjectId:X}", type = o.ObjectType.ToString(),
    model = o.ModelName.TrimEnd('\0'), mset = o.AnimationName.TrimEnd('\0'), name = names.GetValueOrDefault(o.ObjectId) });

// Motions per mset: motion id = entry index / 4 (OpenKH's MotionSet), each entry with its raw index too: the player's
// actor+0x180 is the motion id, but a chest's 151-153 match raw indexes (VERIFIED_OFFSETS L185).
var motions = new SortedDictionary<string, object>(StringComparer.OrdinalIgnoreCase);
foreach (var mset in objs.Select(o => o.AnimationName.TrimEnd('\0')).Where(s => s.Length > 0).Distinct(StringComparer.OrdinalIgnoreCase))
{
    var path = Path.Combine(data, "obj", mset);
    if (!File.Exists(path)) continue;
    motions[mset] = ReadBar(path).Select((e, i) => (e, i)).Where(x => x.e.Type != Bar.EntryType.Dummy && x.e.Stream.Length > 0)
        .GroupBy(x => x.i / 4).Select(g => new { id = g.Key,
            name = MotionSet.IsNamedMotion(g.Key) ? ((MotionSet.MotionName)g.Key).ToString() : null,
            entries = g.Select(x => new { index = x.i, name = x.e.Name }).ToArray() }).ToArray();
}

// Items (03system.bin 'item'), abilities included (type Ability).
var item = Item.Read(ReadBar(Path.Combine(data, "03system.bin")).First(e => e.Name == "item").Stream);
var items = item.Items.Select(i => new { id = i.Id, hex = $"{i.Id:X}", type = i.Type.ToString(), name = Text(i.Name),
    stat = i.StatEntry, command = i.Command });

// Missions: every msn/us file, with the room programs that name it.
var msnDir = Path.Combine(data, "msn", "us");
foreach (var f in Directory.GetFiles(msnDir, "*.bar"))
    if (!missionUse.ContainsKey(Path.GetFileNameWithoutExtension(f))) missionUse[Path.GetFileNameWithoutExtension(f)] = new();
var missions = missionUse.Select(kv => new { name = kv.Key, has_file = File.Exists(Path.Combine(msnDir, kv.Key + ".bar")), used_in = kv.Value });

var opts = new JsonSerializerOptions { WriteIndented = true, Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping };
void Save(string name, object value) => File.WriteAllText(Path.Combine(outDir, name + ".json"), JsonSerializer.Serialize(value, opts));
Save("rooms", rooms); Save("objects", objects); Save("motions", motions); Save("items", items); Save("missions", missions);
Console.WriteLine($"catalog: {rooms.Count} rooms, {objs.Count} objects, {motions.Count} motion sets, {item.Items.Count} items, " +
                  $"{missionUse.Count} missions -> {outDir}");
