using System.Drawing;
using System.Drawing.Imaging;
using System.Text.Json;

const int TextureSize = 32;
var kindOrder = new[]
{
    "lowland", "midland", "highland", "water", "mountain", "iron", "copper", "coal"
};

var options = ParseArgs(args);
var rng = new Random(options.Seed);

if (!string.IsNullOrWhiteSpace(options.ExportHqTilesDir))
{
    GenerateHqTiles(options);
    Console.WriteLine("[AI-TRAINER] HQ seamless tiles generated.");
    return;
}

Console.WriteLine("[AI-TRAINER] Starting style training...");
Console.WriteLine($"[AI-TRAINER] dataset={options.DatasetRoot}");
Console.WriteLine($"[AI-TRAINER] output={options.OutputFile}");
Console.WriteLine($"[AI-TRAINER] profile=factory-2.5d");
if (!string.IsNullOrWhiteSpace(options.StyleShotsDir)) Console.WriteLine($"[AI-TRAINER] style-shots={options.StyleShotsDir}");

// HQ tiles: assets/generated/hq-tiles/32/{hqKind}/{hqKind}_v{N:D2}_32.png
// Mapping from atlas kind → hq-tiles folder name.
var hqTilesRoot = Path.Combine("assets", "generated", "hq-tiles", "32");
var hqKindMap = new Dictionary<string, string>
{
    { "lowland",  "grass"  },
    { "midland",  "dirt"   },
    { "highland", "rocky"  },
    { "mountain", "rocky"  },
    { "water",    "water"       }, // no hq water tile — fall through to SD
    { "iron",     "iron"   },
    { "copper",   "copper" },
    { "coal",     "coal"   },
    { "player",   ""       }, // no hq player sprite — fall through to SD
};

// Returns sorted list of pre-made 32px PNGs for this kind (up to requested count).
List<string> FindHqPaths(string kind)
{
    if (!hqKindMap.TryGetValue(kind, out var hqName) || string.IsNullOrEmpty(hqName))
        return [];
    var dir = Path.Combine(hqTilesRoot, hqName);
    if (!Directory.Exists(dir)) return [];
    return Directory.GetFiles(dir, $"{hqName}_v*_32.png").OrderBy(f => f).ToList();
}

// Hardcoded texture paths — UNIVERSAL (works on any drive)
// Find assets folder by searching up directory tree
var searchDir = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
string textureBasePath = null;
for (int i = 0; i < 6; i++)
{
    var candidate = Path.Combine(searchDir, "assets", "generated", "sd_tiles_qhd_realistic");
    if (Directory.Exists(candidate))
    {
        textureBasePath = candidate;
        break;
    }
    var parent = Path.GetDirectoryName(searchDir);
    if (parent == searchDir) break; // Reached root
    searchDir = parent;
}
if (textureBasePath == null)
{
    // Fallback: assume running from project root
    textureBasePath = Path.GetFullPath(Path.Combine("assets", "generated", "sd_tiles_qhd_realistic"));
}

var hardcodedTextures = new Dictionary<string, string>
{
    { "lowland",  Path.Combine(textureBasePath, "lowland_1024.png") },
    { "midland",  Path.Combine(textureBasePath, "midland_1024.png") },
    { "highland", Path.Combine(textureBasePath, "highland_1024.png") },
    { "water",    Path.Combine(textureBasePath, "..", "sd_tiles_qhd", "water_1024.png") },
    { "mountain", Path.Combine(textureBasePath, "mountain_1024.png") },
    { "iron",     Path.Combine(textureBasePath, "iron_1024.png") },
    { "copper",   Path.Combine(textureBasePath, "copper_1024.png") },
    { "coal",     Path.Combine(textureBasePath, "coal_1024.png") }
};

var waterMutatedPath = Path.Combine(textureBasePath, "water_1024.png");

Console.WriteLine($"[AI-TRAINER] Texture path: {textureBasePath}");

var allTexturesExist = hardcodedTextures.All(kvp => File.Exists(kvp.Value));
if (allTexturesExist)
{
    Console.WriteLine("[AI-TRAINER] ✓ All hardcoded SD-realistic textures found!");
}
else
{
    Console.WriteLine("[AI-TRAINER] ✗ Missing textures!");
    foreach (var kvp in hardcodedTextures)
        Console.WriteLine($"  {kvp.Key}: {(File.Exists(kvp.Value) ? "✓" : "✗")} {kvp.Value}");
}

var globalStyleSamples = LoadGlobalStyleSamples(options.StyleShotsDir, rng);
Console.WriteLine($"[AI-TRAINER] global-style-samples={globalStyleSamples.Count}");

var variantsByKind = new List<List<uint[]>>(kindOrder.Length);
for (var i = 0; i < kindOrder.Length; i++)
{
    var kind = kindOrder[i];
    var files = EnumerateImageFiles(Path.Combine(options.DatasetRoot, kind)).ToList();
    var refs = LoadReferenceSamples(files, rng);
    var allRefs = new List<ColorF>(refs.Count + globalStyleSamples.Count);
    allRefs.AddRange(refs);
    allRefs.AddRange(globalStyleSamples);
    Console.WriteLine($"[AI-TRAINER] kind={kind} refs={files.Count} styleRefs={allRefs.Count} variants={options.Variants}");

    var variants = new List<uint[]>(options.Variants);

    var sdPath = hardcodedTextures.ContainsKey(kind) ? hardcodedTextures[kind] : null;
    var sdMutatedWaterPath = kind == "water" ? waterMutatedPath : null;
    var sdIsRealistic = true;

    if (sdPath != null && File.Exists(sdPath))
    {
        var label = sdIsRealistic ? "SD-realistic" : "SD";
        Console.WriteLine($"[AI-TRAINER] kind={kind} source={label} variants={options.Variants}");
        using var sdImg = new Bitmap(sdPath);
        // TODO: Water mutation disabled until pollution system is implemented
        // if (kind == "water" && sdMutatedWaterPath != null)
        // {
        //     using var mutatedImg = new Bitmap(sdMutatedWaterPath);
        //     for (var v = 0; v < options.Variants; v++)
        //     {
        //         var src = v == 1 ? mutatedImg : sdImg;
        //         using var tex = ResampleWithVariation(src, TextureSize, options.Seed + i * 997 + v * 41, v);
        //         variants.Add(ToPackedPixels(tex));
        //     }
        // }
        // else
        {
            for (var v = 0; v < options.Variants; v++)
            {
                using var tex = ResampleWithVariation(sdImg, TextureSize, options.Seed + i * 997 + v * 41, v);
                variants.Add(ToPackedPixels(tex));
            }
        }
    }
    else
    {
        Console.WriteLine($"[AI-TRAINER] kind={kind} source=proc refs={files.Count} styleRefs={allRefs.Count} variants={options.Variants}");
        for (var v = 0; v < options.Variants; v++)
        {
            using var tex = GenerateTexture(kind, allRefs, rng, options.Seed + i * 997 + v * 41, options.StyleBlend);
            variants.Add(ToPackedPixels(tex));
        }
    }
    variantsByKind.Add(variants);
}

WriteAtlas(options.OutputFile, kindOrder.Length, options.Variants, variantsByKind);
Console.WriteLine("[AI-TRAINER] Done. Atlas generated.");

return;

static TrainerOptions ParseArgs(string[] args)
{
    var options = new TrainerOptions();
    for (var i = 0; i < args.Length; i++)
    {
        var a = args[i];
        if (a == "--dataset-root" && i + 1 < args.Length) options.DatasetRoot = args[++i];
        else if (a == "--output" && i + 1 < args.Length) options.OutputFile = args[++i];
        else if (a == "--variants" && i + 1 < args.Length && int.TryParse(args[++i], out var variants)) options.Variants = Math.Clamp(variants, 1, 32);
        else if (a == "--seed" && i + 1 < args.Length && int.TryParse(args[++i], out var seed)) options.Seed = seed;
        else if (a == "--export-hq-tiles" && i + 1 < args.Length) options.ExportHqTilesDir = args[++i];
        else if (a == "--tile-sizes" && i + 1 < args.Length) options.TileSizes = args[++i];
        else if (a == "--tile-variants" && i + 1 < args.Length && int.TryParse(args[++i], out var tileVariants)) options.TileVariants = Math.Clamp(tileVariants, 1, 64);
        else if (a == "--style-shots-dir" && i + 1 < args.Length) options.StyleShotsDir = args[++i];
        else if (a == "--style-blend" && i + 1 < args.Length && float.TryParse(args[++i], out var sb)) options.StyleBlend = Math.Clamp(sb, 0.0f, 0.6f);
        else if (a is "--help" or "-h")
        {
            PrintHelp();
            Environment.Exit(0);
        }
    }

    if (string.IsNullOrWhiteSpace(options.DatasetRoot)) options.DatasetRoot = @"assets\style-dataset";
    if (string.IsNullOrWhiteSpace(options.OutputFile)) options.OutputFile = @"assets\generated\runtime_texture_atlas.bin";
    return options;
}

static void PrintHelp()
{
    Console.WriteLine("FactorioPt.AiTrainer");
    Console.WriteLine("  --dataset-root <path>   Root with class folders (lowland/midland/...)");
    Console.WriteLine("  --output <file>         Output atlas binary path");
    Console.WriteLine("  --variants <n>          Variants per class (1..32, default 8 recommended)");
    Console.WriteLine("  --seed <n>              RNG seed");
    Console.WriteLine("  --export-hq-tiles <dir> Export seamless HQ PNG tiles (terrain + ore)");
    Console.WriteLine("  --tile-sizes <list>     Comma-separated tile sizes for HQ export (default: 32,64)");
    Console.WriteLine("  --tile-variants <n>     Variants per kind for HQ export (default: 8)");
    Console.WriteLine("  --style-shots-dir <dir> Folder with reference screenshots (top-level files only)");
    Console.WriteLine("  --style-blend <0..0.6>  Global style blend amount (default: 0.28)");
}

static IEnumerable<string> EnumerateImageFiles(string directory)
{
    if (!Directory.Exists(directory)) yield break;
    var exts = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { ".png", ".jpg", ".jpeg", ".bmp", ".webp" };
    foreach (var file in Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories))
    {
        if (exts.Contains(Path.GetExtension(file))) yield return file;
    }
}

static List<ColorF> LoadGlobalStyleSamples(string directory, Random rng)
{
    var samples = new List<ColorF>(2048);
    if (string.IsNullOrWhiteSpace(directory) || !Directory.Exists(directory)) return samples;

    var exts = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { ".png", ".jpg", ".jpeg", ".bmp", ".webp" };
    var files = Directory.EnumerateFiles(directory, "*", SearchOption.TopDirectoryOnly)
        .Where(f => exts.Contains(Path.GetExtension(f)))
        .Take(24)
        .ToList();
    foreach (var file in files)
    {
        try
        {
            using var bmp = new Bitmap(file);
            if (bmp.Width < 2 || bmp.Height < 2) continue;
            var sampleCount = Math.Clamp((bmp.Width * bmp.Height) / 2048, 64, 256);
            for (var i = 0; i < sampleCount; i++)
            {
                var x = rng.Next(bmp.Width);
                var y = rng.Next(bmp.Height);
                samples.Add(ColorF.FromColor(bmp.GetPixel(x, y)));
            }
        }
        catch
        {
            // ignore invalid file
        }
    }
    return samples;
}

static List<ColorF> LoadReferenceSamples(IReadOnlyList<string> files, Random rng)
{
    var samples = new List<ColorF>(256);
    foreach (var file in files.Take(24))
    {
        try
        {
            using var bmp = new Bitmap(file);
            if (bmp.Width < 2 || bmp.Height < 2) continue;
            var sampleCount = Math.Min(64, Math.Max(8, (bmp.Width * bmp.Height) / 4096));
            for (var i = 0; i < sampleCount; i++)
            {
                var x = rng.Next(bmp.Width);
                var y = rng.Next(bmp.Height);
                var c = bmp.GetPixel(x, y);
                samples.Add(ColorF.FromColor(c));
            }
        }
        catch
        {
            // Skip invalid input files.
        }
    }
    return samples;
}

// Downsample an SD-generated high-res PNG to TextureSize with subtle per-variant tint/offset for variety.
static Bitmap ResampleWithVariation(Bitmap src, int dstSize, int seed, int variantIndex)
{
    // Clean bicubic downscale — no tinting, no color modification, no variant shifts.
    var dst = new Bitmap(dstSize, dstSize, PixelFormat.Format32bppArgb);
    using var g = Graphics.FromImage(dst);
    g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
    g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.HighQuality;
    g.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
    g.CompositingQuality = System.Drawing.Drawing2D.CompositingQuality.HighQuality;
    g.DrawImage(src, 0, 0, dstSize, dstSize);
    return dst;
}

static Bitmap GenerateTexture(string kind, IReadOnlyList<ColorF> refSamples, Random rng, int seed, float styleBlendOverride)
{
    var output = new Bitmap(TextureSize, TextureSize, PixelFormat.Format24bppRgb);

    for (var y = 0; y < TextureSize; y++)
    {
        for (var x = 0; x < TextureSize; x++)
        {
            var nMacro = Fbm((x + 13.7f) * 0.090f, (y + 29.4f) * 0.090f, seed ^ 0x132451, 4);
            var nMicro = Fbm((x + 3.1f) * 0.48f, (y + 7.9f) * 0.48f, seed ^ 0x823147, 3);
            var nFine = Fbm((x + 1.7f) * 1.20f, (y + 5.3f) * 1.20f, seed ^ 0x934521, 2);
            // Strong directional light + noise-driven shade for high contrast.
            var light = 0.90f + ((x - y) / (float)(TextureSize * 2)) * 0.20f;
            var shade = light + (nMacro - 0.5f) * 0.44f + (nMicro - 0.5f) * 0.20f;

            var c = kind switch
            {
                "water" => GenerateWaterColor(nMacro, nMicro, x, y, shade, seed),
                "mountain" => GenerateMountainColor(nMacro, nMicro, nFine, x, y, shade, seed),
                "iron" => GenerateIronColor(nMacro, nMicro, x, y, shade, seed),
                "copper" => GenerateCopperColor(nMacro, nMicro, x, y, shade, seed),
                "coal" => GenerateCoalColor(nMacro, nMicro, x, y, shade, seed),
                "player" => GeneratePlayerColor(x, y, shade),
                _ => GenerateTerrainColor(kind, nMacro, nMicro, nFine, x, y, shade, seed)
            };

            c = ApplyPixelAccent(c, x, y, kind, seed);
            output.SetPixel(x, y, c.ToColor());
        }
    }

    return output;
}

static ColorF GenerateTerrainColor(string kind, float nMacro, float nMicro, float nFine, int x, int y, float shade, int seed)
{
    // Factorio-accurate palette: warm sandy-brown dominant, muted olive grass, pale stone.
    var (dark, light) = kind switch
    {
        // Grass: dark olive green, sparse — not vivid, muted like real Factorio
        "lowland" => (new ColorF(48, 76, 28), new ColorF(94, 130, 56)),
        // Dirt: warm sandy brown — this is the DOMINANT Factorio terrain color
        "midland" => (new ColorF(96, 74, 44), new ColorF(158, 122, 74)),
        // Highland/sandy rock: pale warm beige
        _ => (new ColorF(128, 104, 66), new ColorF(192, 158, 104))
    };

    var n = Math.Clamp((nMacro * 0.60f + nMicro * 0.40f - 0.5f) * 1.5f + 0.5f, 0f, 1f);
    var c = Lerp(dark, light, n);
    c = Mul(c, shade);

    if (kind == "lowland")
    {
        // Sparse dark grass blades (NOT vivid — muted olive)
        if (Hash01(x, y, seed ^ 0x292) > 0.80f && nMicro > 0.55f)
            c = Add(c, new ColorF(-6, 18, -8));
        // Brown soil patches showing through grass
        if (Hash01(x * 2, y * 3, seed ^ 0x519) < 0.12f)
            c = Add(c, new ColorF(22, 10, -4));
        // Tiny pebbles
        if (Hash01(x * 5, y * 7, seed ^ 0x731) > 0.96f)
            c = Add(c, new ColorF(14, 10, 6));
        // Micro height variation in green channel
        c = Add(c, new ColorF(0f, (nFine - 0.5f) * 10f, 0f));
    }
    else if (kind == "midland")
    {
        // Fine soil grain — warm reddish highlights
        if ((x * 3 + y * 7) % 11 < 2 && Hash01(x, y, seed ^ 0x411) > 0.65f)
            c = Add(c, new ColorF(10, 6, 2));
        // Dark dry compressed soil patches
        if (Hash01(x * 4, y * 3, seed ^ 0x621) > 0.85f && nMicro > 0.62f)
            c = Add(c, new ColorF(-22, -16, -10));
        // Sandy highlight spots
        if (Hash01(x * 3, y * 5, seed ^ 0x841) > 0.90f && nMacro > 0.55f)
            c = Add(c, new ColorF(18, 12, 6));
        c = Add(c, new ColorF((nFine - 0.5f) * 8f, (nFine - 0.5f) * 6f, (nFine - 0.5f) * 3f));
    }
    else // highland
    {
        // Deep dry crack lines
        if (Hash01(x * 5, y * 7, seed ^ 0x393) > 0.74f && nMicro > 0.65f)
            c = Add(c, new ColorF(-26, -22, -14));
        // Pale flat stone surfaces
        if (Hash01(x * 3, y * 4, seed ^ 0x812) > 0.88f && nMacro > 0.54f)
            c = Add(c, new ColorF(20, 14, 8));
        // Small stones
        if (Hash01(x * 7, y * 5, seed ^ 0x913) > 0.94f)
            c = Add(c, new ColorF(18, 14, 8));
        if (Hash01(x * 9, y * 3, seed ^ 0xA13) > 0.96f)
            c = Add(c, new ColorF(-20, -16, -10));  // dark pebble
    }

    return Clamp(c);
}

static ColorF GenerateWaterColor(float nMacro, float nMicro, int x, int y, float shade, int seed)
{
    var deep = new ColorF(16, 68, 152);
    var shallow = new ColorF(60, 148, 218);
    var n = Math.Clamp((nMacro * 0.65f + nMicro * 0.35f - 0.5f) * 1.5f + 0.5f, 0f, 1f);
    var c = Lerp(deep, shallow, n);
    // Deep cold patches
    if (nMacro < 0.30f) c = Add(c, new ColorF(-16, -10, -28));
    // Wave sparkle highlights
    if (Hash01(x, y, seed ^ 0x9A1) > 0.97f) c = Add(c, new ColorF(80, 100, 100));
    return Mul(Clamp(c), shade * 0.97f);
}

static ColorF GenerateMountainColor(float nMacro, float nMicro, float nFine, int x, int y, float shade, int seed)
{
    var rockDark = new ColorF(52, 49, 44);
    var rockLight = new ColorF(116, 110, 100);
    var n = Math.Clamp((nMacro - 0.5f) * 1.7f + 0.5f, 0f, 1f);
    var c = Lerp(rockDark, rockLight, n);
    // Strong deep cracks
    if (Hash01(x * 3, y * 5, seed ^ 0x901) > 0.76f && nMicro > 0.65f)
        c = Add(c, new ColorF(-34, -30, -26));
    // Fine crack lines
    if (nFine > 0.80f && nMicro > 0.55f)
        c = Add(c, new ColorF(-22, -20, -17));
    // Bright rock faces
    if (nMicro > 0.82f && nMacro > 0.52f)
        c = Add(c, new ColorF(24, 22, 18));
    // Rare mineral glint
    if (Hash01(x * 5, y * 7, seed ^ 0xA23) > 0.97f)
        c = Add(c, new ColorF(30, 26, 20));
    return Mul(Clamp(c), shade * 0.87f);
}

static ColorF GenerateIronColor(float nMacro, float nMicro, int x, int y, float shade, int seed)
{
    var dark = new ColorF(64, 84, 112);
    var bright = new ColorF(162, 188, 218);
    var n = Math.Clamp((nMacro - 0.5f) * 1.7f + 0.5f, 0f, 1f);
    var c = Lerp(dark, bright, n);
    // Strong dark metallic veins
    var vein = nMicro * 0.7f + nMacro * 0.3f;
    if (vein > 0.68f) c = Add(c, new ColorF(-40, -36, -32));
    // Deep shadow pits
    if (vein < 0.22f) c = Add(c, new ColorF(-28, -25, -22));
    // Blue metallic sheen — iron's defining characteristic
    c = Add(c, new ColorF(0f, 0f, (nMacro - 0.5f) * 36f));
    // Mineral sparkle
    if (Hash01(x, y, seed ^ 0xA91) > 0.96f) c = Add(c, new ColorF(26, 34, 52));
    return Mul(Clamp(c), shade);
}

static ColorF GenerateCopperColor(float nMacro, float nMicro, int x, int y, float shade, int seed)
{
    var dark = new ColorF(132, 48, 14);
    var bright = new ColorF(248, 132, 58);
    var n = Math.Clamp((nMacro - 0.5f) * 1.7f + 0.5f, 0f, 1f);
    var c = Lerp(dark, bright, n);
    // Oxidation (green-teal) patches
    if (Hash01(x, y, seed ^ 0xB91) > 0.86f && nMicro > 0.66f)
        c = Add(c, new ColorF(-36, 26, 18));
    // Dark shadow pits
    if (nMicro < 0.20f) c = Add(c, new ColorF(-34, -26, -16));
    // Vivid orange highlight
    if (Hash01(x * 2, y * 3, seed ^ 0xC91) > 0.92f && nMacro > 0.50f)
        c = Add(c, new ColorF(44, 20, 4));
    return Mul(Clamp(c), shade);
}

static ColorF GenerateCoalColor(float nMacro, float nMicro, int x, int y, float shade, int seed)
{
    var dark = new ColorF(12, 12, 16);
    var seam = new ColorF(62, 64, 74);
    var n = Math.Clamp(nMacro * 1.4f, 0f, 1f);
    var c = Lerp(dark, seam, n * 0.50f);  // Overall very dark
    // Bright carbon seam veins
    if (Hash01(x * 3, y * 7, seed ^ 0xD91) > 0.80f && nMicro > 0.70f)
        c = Add(c, new ColorF(46, 46, 52));
    // Ink-black pits
    if (nMacro < 0.26f) c = Add(c, new ColorF(-10, -10, -8));
    // Rare coal facet highlight
    if (Hash01(x * 5, y * 9, seed ^ 0xE12) > 0.97f)
        c = Add(c, new ColorF(38, 38, 44));
    return Mul(Clamp(c), shade * 0.87f);
}

static ColorF GeneratePlayerColor(int x, int y, float shade)
{
    var c = new ColorF(222, 208, 160);
    var centerDist = MathF.Abs(x - TextureSize / 2f) + MathF.Abs(y - TextureSize / 2f);
    if (centerDist < 4f) c = new ColorF(96, 198, 214);
    return Mul(c, shade * 0.94f);
}

static ColorF ApplyPixelAccent(ColorF c, int x, int y, string kind, int seed)
{
    var dither = ((x + y + (seed & 7)) & 1) == 0;
    if (dither) c = Add(c, new ColorF(3, 3, 3));
    if (kind is "iron" or "copper" or "coal")
    {
        var edge = x == 0 || y == 0 || x == TextureSize - 1 || y == TextureSize - 1;
        if (edge) c = Add(c, new ColorF(-8, -8, -8));
    }
    return Clamp(c);
}

static uint[] ToPackedPixels(Bitmap bitmap)
{
    var pixels = new uint[TextureSize * TextureSize];
    var i = 0;
    for (var y = 0; y < TextureSize; y++)
    {
        for (var x = 0; x < TextureSize; x++)
        {
            var c = bitmap.GetPixel(x, y);
            pixels[i++] = (uint)c.R | ((uint)c.G << 8) | ((uint)c.B << 16);
        }
    }
    return pixels;
}

static void WriteAtlas(string outputFile, int kindCount, int variantsPerKind, IReadOnlyList<List<uint[]>> variantsByKind)
{
    var fullPath = Path.GetFullPath(outputFile);
    Directory.CreateDirectory(Path.GetDirectoryName(fullPath)!);
    using var fs = new FileStream(fullPath, FileMode.Create, FileAccess.Write, FileShare.None);
    using var bw = new BinaryWriter(fs);
    bw.Write(new[] { (byte)'F', (byte)'P', (byte)'T', (byte)'A' });
    bw.Write(1u); // version
    bw.Write((uint)TextureSize);
    bw.Write((uint)kindCount);
    for (var kind = 0; kind < kindCount; kind++)
    {
        bw.Write((uint)kind);
        bw.Write((uint)variantsPerKind);
        foreach (var tex in variantsByKind[kind])
        {
            foreach (var pixel in tex) bw.Write(pixel);
        }
    }
}

static string[] GetHqKinds() => ["dirt", "grass", "sand", "rocky", "iron", "copper", "coal"];

static void GenerateHqTiles(TrainerOptions options)
{
    var outputRoot = Path.GetFullPath(options.ExportHqTilesDir);
    Directory.CreateDirectory(outputRoot);
    var sizes = ParseSizes(options.TileSizes);
    var rng = new Random(options.Seed);
    var globalStyleSamples = LoadGlobalStyleSamples(options.StyleShotsDir, rng);

    var manifest = new Dictionary<string, Dictionary<string, List<string>>>(StringComparer.OrdinalIgnoreCase);
    Console.WriteLine($"[AI-TRAINER] Generating HQ tiles -> {outputRoot}");
    Console.WriteLine($"[AI-TRAINER] HQ style-shots={options.StyleShotsDir}");
    Console.WriteLine($"[AI-TRAINER] HQ global-style-samples={globalStyleSamples.Count}");

    var hqKinds = GetHqKinds();
    foreach (var size in sizes)
    {
        var sizeDir = Path.Combine(outputRoot, size.ToString());
        Directory.CreateDirectory(sizeDir);
        var byKind = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);

        for (var kindIndex = 0; kindIndex < hqKinds.Length; kindIndex++)
        {
            var kind = hqKinds[kindIndex];
            var kindDir = Path.Combine(sizeDir, kind);
            Directory.CreateDirectory(kindDir);
            var files = new List<string>(options.TileVariants);

            for (var v = 0; v < options.TileVariants; v++)
            {
                var seed = options.Seed ^ (size * 1009) ^ (kindIndex * 131) ^ (v * 8191);
                using var tile = GenerateHqTile(kind, size, seed, globalStyleSamples, options.StyleBlend);
                var fileName = $"{kind}_v{v + 1:00}_{size}.png";
                var fullPath = Path.Combine(kindDir, fileName);
                tile.Save(fullPath, ImageFormat.Png);
                files.Add($"{size}/{kind}/{fileName}");
            }

            byKind[kind] = files;
            Console.WriteLine($"[AI-TRAINER] [{size}px] {kind}: {files.Count} variants");
        }

        WritePreviewSheet(sizeDir, size, options.TileVariants, hqKinds);
        manifest[size.ToString()] = byKind;
    }

    var manifestPayload = new
    {
        generator = "tools-csharp/ai-trainer (HQ mode)",
        styles = "top-down orthographic, seamless, no directional shadows",
        sizes,
        variants = options.TileVariants,
        kinds = hqKinds,
        files = manifest
    };
    var manifestPath = Path.Combine(outputRoot, "manifest.json");
    File.WriteAllText(manifestPath, JsonSerializer.Serialize(manifestPayload, new JsonSerializerOptions { WriteIndented = true }));
    Console.WriteLine($"[AI-TRAINER] Manifest written: {manifestPath}");
}

static List<int> ParseSizes(string text)
{
    var outSizes = new List<int>();
    var parts = text.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
    foreach (var p in parts)
    {
        if (int.TryParse(p, out var v))
        {
            outSizes.Add(Math.Clamp(v, 8, 512));
        }
    }
    if (outSizes.Count == 0) outSizes.Add(32);
    return outSizes;
}

static void WritePreviewSheet(string sizeDir, int size, int variants, string[] hqKinds)
{
    using var sheet = new Bitmap(size * variants, size * hqKinds.Length, PixelFormat.Format24bppRgb);
    using var g = Graphics.FromImage(sheet);
    g.Clear(Color.Black);
    g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.NearestNeighbor;
    g.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.Half;

    for (var row = 0; row < hqKinds.Length; row++)
    {
        var kind = hqKinds[row];
        for (var col = 0; col < variants; col++)
        {
            var p = Path.Combine(sizeDir, kind, $"{kind}_v{col + 1:00}_{size}.png");
            if (!File.Exists(p)) continue;
            using var tile = new Bitmap(p);
            g.DrawImage(tile, col * size, row * size, size, size);
        }
    }

    var outPath = Path.Combine(sizeDir, $"preview_sheet_{size}.png");
    sheet.Save(outPath, ImageFormat.Png);
}

static Bitmap GenerateHqTile(string kind, int size, int seed, IReadOnlyList<ColorF> styleRefs, float styleBlend)
{
    var bmp = new Bitmap(size, size, PixelFormat.Format24bppRgb);
    for (var y = 0; y < size; y++)
    {
        for (var x = 0; x < size; x++)
        {
            var c = (kind is "dirt" or "grass" or "sand" or "rocky")
                ? HqTerrainColor(kind, x, y, size, seed)
                : HqOreColor(kind, x, y, size, seed);
            if (styleRefs.Count > 0 && styleBlend > 0.0f)
            {
                var idx = Math.Abs((x * 131 + y * 61 + seed) % styleRefs.Count);
                c = Lerp(c, styleRefs[idx], Math.Clamp(styleBlend * 0.35f, 0.0f, 0.35f));
            }
            bmp.SetPixel(x, y, c.ToColor());
        }
    }
    return bmp;
}

static ColorF HqTerrainColor(string kind, int x, int y, int size, int seed)
{
    var macro = HqFbmPeriodic(x * 0.11f, y * 0.11f, size, seed ^ 0xA11, 4);
    var micro = HqFbmPeriodic(x * 0.49f, y * 0.49f, size, seed ^ 0xA22, 3);
    var grain = HqFbmPeriodic(x * 0.95f, y * 0.95f, size, seed ^ 0xA33, 2);
    var spots = HqHash01Periodic(x, y, seed ^ 0xA44, size);

    if (kind == "dirt")
    {
        var baseC = Lerp(new ColorF(92, 64, 42), new ColorF(142, 98, 62), macro);
        var layers = HqFbmPeriodic(x * 0.14f, y * 0.26f, size, seed ^ 0xA55, 3);
        var c = Add(baseC, new ColorF((layers - 0.5f) * 18f, (layers - 0.5f) * 10f, (layers - 0.5f) * 6f));
        c = Add(c, new ColorF((grain - 0.5f) * 12f, (micro - 0.5f) * 10f, (micro - 0.5f) * 9f));
        if (spots > 0.965f) c = Add(c, new ColorF(18, 14, 10));
        return Clamp(c);
    }

    if (kind == "grass")
    {
        var baseC = Lerp(new ColorF(66, 110, 62), new ColorF(102, 142, 86), macro);
        var tuft = HqFbmPeriodic(x * 0.30f, y * 0.30f, size, seed ^ 0xA66, 4);
        var c = Add(baseC, new ColorF((tuft - 0.5f) * 8f, (tuft - 0.5f) * 16f, (tuft - 0.5f) * 7f));
        c = Add(c, new ColorF((micro - 0.5f) * 5f, (grain - 0.5f) * 8f, (micro - 0.5f) * 4f));
        if (spots > 0.972f) c = Add(c, new ColorF(14, 18, 10));
        return Clamp(c);
    }

    if (kind == "sand")
    {
        var baseC = Lerp(new ColorF(198, 176, 126), new ColorF(228, 208, 152), macro);
        var c = Add(baseC, new ColorF((grain - 0.5f) * 10f, (micro - 0.5f) * 10f, (grain - 0.5f) * 8f));
        if (spots > 0.968f) c = Add(c, new ColorF(-18, -16, -12));
        return Clamp(c);
    }

    {
        var baseC = Lerp(new ColorF(92, 86, 80), new ColorF(134, 124, 112), macro);
        var crack = HqFbmPeriodic(x * 0.36f, y * 0.36f, size, seed ^ 0xA77, 4);
        var veins = HqFbmPeriodic(x * 0.20f + 11.0f, y * 0.20f - 7.0f, size, seed ^ 0xA88, 4);
        var c = Add(baseC, new ColorF((crack - 0.5f) * 18f, (crack - 0.5f) * 16f, (crack - 0.5f) * 14f));
        if (veins > 0.73f) c = Add(c, new ColorF(12, 10, 8));
        if (spots > 0.972f) c = Add(c, new ColorF(-16, -14, -12));
        return Clamp(c);
    }
}

static ColorF HqOreColor(string kind, int x, int y, int size, int seed)
{
    var field = HqFbmPeriodic(x * 0.15f, y * 0.15f, size, seed ^ 0xB11, 4);
    var lumps = HqFbmPeriodic(x * 0.42f, y * 0.42f, size, seed ^ 0xB22, 4);
    var grit = HqFbmPeriodic(x * 0.95f, y * 0.95f, size, seed ^ 0xB33, 2);
    var spark = HqHash01Periodic(x, y, seed ^ 0xB44, size);
    var cluster = HqSmoothStep(0.50f, 0.84f, field * 0.72f + lumps * 0.58f);

    if (kind == "iron")
    {
        var baseC = Lerp(new ColorF(82, 96, 110), new ColorF(118, 134, 148), field);
        var ore = Lerp(new ColorF(120, 138, 156), new ColorF(170, 188, 202), lumps);
        var c = Lerp(baseC, ore, cluster);
        c = Add(c, new ColorF((grit - 0.5f) * 10f, (grit - 0.5f) * 11f, (grit - 0.5f) * 13f));
        if (spark > 0.985f) c = Add(c, new ColorF(28, 32, 36));
        return Clamp(c);
    }

    if (kind == "copper")
    {
        var baseC = Lerp(new ColorF(106, 64, 40), new ColorF(144, 86, 54), field);
        var ore = Lerp(new ColorF(162, 98, 60), new ColorF(206, 132, 84), lumps);
        var c = Lerp(baseC, ore, cluster);
        c = Add(c, new ColorF((grit - 0.5f) * 9f, (grit - 0.5f) * 7f, (grit - 0.5f) * 5f));
        if (spark > 0.986f) c = Add(c, new ColorF(24, 15, 8));
        return Clamp(c);
    }

    {
        var baseC = Lerp(new ColorF(24, 26, 30), new ColorF(42, 46, 52), field);
        var ore = Lerp(new ColorF(38, 40, 46), new ColorF(64, 68, 74), lumps);
        var c = Lerp(baseC, ore, cluster);
        c = Add(c, new ColorF((grit - 0.5f) * 7f, (grit - 0.5f) * 7f, (grit - 0.5f) * 7f));
        if (spark > 0.989f) c = Add(c, new ColorF(16, 16, 16));
        return Clamp(c);
    }
}

static float HqHash01Periodic(int x, int y, int seed, int period)
{
    var xx = ((x % period) + period) % period;
    var yy = ((y % period) + period) % period;
    return Hash01(xx, yy, seed);
}

static float HqValueNoisePeriodic(float x, float y, int period, int seed)
{
    var x0 = (int)MathF.Floor(x);
    var y0 = (int)MathF.Floor(y);
    var x1 = x0 + 1;
    var y1 = y0 + 1;
    var tx = Smooth(x - x0);
    var ty = Smooth(y - y0);

    var a = HqHash01Periodic(x0, y0, seed, period);
    var b = HqHash01Periodic(x1, y0, seed, period);
    var c = HqHash01Periodic(x0, y1, seed, period);
    var d = HqHash01Periodic(x1, y1, seed, period);
    var ab = a + (b - a) * tx;
    var cd = c + (d - c) * tx;
    return ab + (cd - ab) * ty;
}

static float HqFbmPeriodic(float x, float y, int period, int seed, int octaves)
{
    var sum = 0f;
    var amp = 0.5f;
    var freq = 1f;
    var norm = 0f;
    for (var i = 0; i < octaves; i++)
    {
        var per = Math.Max(1, (int)MathF.Round(period * freq));
        sum += HqValueNoisePeriodic(x * freq, y * freq, per, seed + i * 977) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2f;
    }
    return norm > 0f ? sum / norm : 0f;
}

static float HqSmoothStep(float a, float b, float x)
{
    if (MathF.Abs(a - b) < 1e-6f) return x >= b ? 1f : 0f;
    var t = (x - a) / (b - a);
    return Smooth(Math.Clamp(t, 0f, 1f));
}

static float Hash01(int x, int y, int seed)
{
    unchecked
    {
        uint h = (uint)seed;
        h ^= (uint)x * 374761393u;
        h ^= (uint)y * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (h & 0x00FFFFFFu) / 16777216f;
    }
}

static float ValueNoise(float x, float y, int seed)
{
    var x0 = (int)MathF.Floor(x);
    var y0 = (int)MathF.Floor(y);
    var x1 = x0 + 1;
    var y1 = y0 + 1;
    var tx = Smooth(x - x0);
    var ty = Smooth(y - y0);

    var n00 = Hash01(x0, y0, seed);
    var n10 = Hash01(x1, y0, seed);
    var n01 = Hash01(x0, y1, seed);
    var n11 = Hash01(x1, y1, seed);
    var nx0 = n00 + (n10 - n00) * tx;
    var nx1 = n01 + (n11 - n01) * tx;
    return nx0 + (nx1 - nx0) * ty;
}

static float Fbm(float x, float y, int seed, int octaves)
{
    var sum = 0f;
    var amp = 1f;
    var freq = 1f;
    var norm = 0f;
    for (var i = 0; i < octaves; i++)
    {
        sum += ValueNoise(x * freq, y * freq, seed + i * 977) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2f;
    }
    return norm > 0f ? sum / norm : 0f;
}

static float Smooth(float t) => t * t * (3f - 2f * t);

static float DistanceSq(ColorF a, ColorF b)
{
    var dr = a.R - b.R;
    var dg = a.G - b.G;
    var db = a.B - b.B;
    return dr * dr + dg * dg + db * db;
}

static ColorF Lerp(ColorF a, ColorF b, float t) => new(
    a.R + (b.R - a.R) * t,
    a.G + (b.G - a.G) * t,
    a.B + (b.B - a.B) * t);

static ColorF Mul(ColorF c, float f) => new(c.R * f, c.G * f, c.B * f);
static ColorF Add(ColorF a, ColorF b) => new(a.R + b.R, a.G + b.G, a.B + b.B);
static ColorF Clamp(ColorF c) => new(Math.Clamp(c.R, 0f, 255f), Math.Clamp(c.G, 0f, 255f), Math.Clamp(c.B, 0f, 255f));

file readonly struct ColorF(float r, float g, float b)
{
    public float R { get; } = r;
    public float G { get; } = g;
    public float B { get; } = b;

    public static ColorF operator *(ColorF c, float f) => new(c.R * f, c.G * f, c.B * f);

    public Color ToColor() => Color.FromArgb((int)Math.Clamp(R, 0, 255), (int)Math.Clamp(G, 0, 255), (int)Math.Clamp(B, 0, 255));
    public static ColorF FromColor(Color c) => new(c.R, c.G, c.B);
}

file sealed class TrainerOptions
{
    public string DatasetRoot { get; set; } = @"assets\style-dataset";
    public string OutputFile { get; set; } = @"assets\generated\runtime_texture_atlas.bin";
    public int Variants { get; set; } = 8;
    public int Seed { get; set; } = 20260508;
    public string ExportHqTilesDir { get; set; } = "";
    public string TileSizes { get; set; } = "32,64";
    public int TileVariants { get; set; } = 8;
    public string StyleShotsDir { get; set; } = ".";
    public float StyleBlend { get; set; } = 0.28f;
}
