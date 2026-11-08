# Creates the game feature data asset and the instanced map material. Re-runnable.
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript
#     -script=C:\SF\Mods\FactoryAtlas\Scripts\create_assets.py -unattended -nosplash -NoLogTimes -abslog=<log>
import unreal

ROOT = "/FactoryAtlas"
reg = unreal.AssetRegistryHelpers.get_asset_registry()
reg.scan_paths_synchronous([ROOT], True)
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary


def find(name):
    path = f"{ROOT}/{name}"
    return lib.load_asset(path) if lib.does_asset_exist(path) else unreal.load_object(None, f"{path}.{name}")


# Game feature data
if find("FactoryAtlas") is None:
    cls = unreal.load_class(None, "/Script/FactoryGame.FGGameFeatureData")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    gfd = tools.create_asset("FactoryAtlas", ROOT, cls, factory)
    lib.save_loaded_asset(gfd, only_if_is_dirty=False)
    unreal.log("ATLAS: created game feature data")

# Material
mat = find("M_AtlasInstances")
if mat is None:
    mat = tools.create_asset("M_AtlasInstances", ROOT, unreal.Material, unreal.MaterialFactoryNew())
else:
    mel.delete_all_material_expressions(mat)

mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("num_customized_u_vs", 4)

# Field layout is documented in AtlasTypes.h.
DECODE = """
float hx = floor(B.x / 8192.0);
float hy = fmod(floor(B.x / 16.0), 512.0);
float type = fmod(floor(B.y / 16.0), 1024.0);
float circle = abs(type - 1021.0) < 0.5 ? 1.0 : 0.0;
float marker = (type > 980.5 && type < 1017.5) ? 1.0 : 0.0;
float unit = circle > 0.5 ? 0.4 : 0.1;
float ppm = abs(S.x) / 7500.0;
float2 ph = marker > 0.5 ? float2(hx, hy) : max(float2(hx, hy) * unit * ppm, 0.5);
float2 ext = ph + 1.0;
float zv = fmod(A.x, 128.0) * 128.0 + fmod(A.y, 128.0);
float paint = fmod(B.x, 16.0) * 16.0 + fmod(B.y, 16.0);
float4 tc = Pal.SampleLevel(PalSampler, float2((type + 0.5) / 1024.0, 0.25), 0.0);
float4 pc = Pal.SampleLevel(PalSampler, float2((paint + 0.5) / 1024.0, 0.75), 0.0);
float vis = (tc.a > 0.5 && (type > 980.5 || (zv >= ZMin && zv <= ZMax))) ? 1.0 : 0.0;
float3 color = (PaintMode > 0.5 && paint > 0.5 && type < 980.5) ? pc.rgb : tc.rgb;
float hollow = marker * fmod(paint, 2.0);
// Path pieces (belts, pipes, rails, roads, wires) have palette alpha 200 rather than 255; see RebuildPalette.
float path = (tc.a > 0.5 && tc.a < 0.9) ? 1.0 : 0.0;
"""

CODE_POSITION = DECODE + """
float2 uv = float2(floor(A.x / 128.0), floor(A.y / 128.0)) / 131071.0;
float ang = floor(B.y / 16384.0) / 1024.0 * 6.28318531;
float2 L = C * ext * vis;
float cs = cos(ang);
float sn = sin(ang);
float2 P = O + S * uv + float2(L.x * cs - L.y * sn, L.x * sn + L.y * cs);
return float3(P, 0.0);
"""

CODE_LOCAL = DECODE + "return C * ext;"
CODE_HALF = DECODE + "return ph;"
CODE_COLOR_RG = DECODE + "return color.rg;"
CODE_COLOR_B = DECODE + "return float2(color.b, circle + marker * (2.0 + hollow) - path);"

CODE_COLOR = """
float3 c = float3(RG.x, RG.y, BA.x);
if (BA.y > 1.5)
{
    float r = length(L / max(H, 0.5));
    float edge = saturate((1.0 - r) * H.x);
    float ring = r > 0.62 ? 0.12 : 1.0;
    // Hollow markers (a node already in use) keep only a coloured band and the dark rim.
    if (BA.y > 2.5 && r < 0.4) { edge *= 0.15; }
    return float4(c * ring, edge);
}
if (BA.y > 0.5)
{
    float r = length(L / max(H, 0.5));
    float glow = saturate(1.0 - r);
    return float4(c, glow * (0.25 + 0.35 * glow));
}
// A path piece (belt, pipe, rail, road, wire) is a capsule: round ends that overlap its neighbours by a full
// width, so the pieces join into one smooth band. No outline, which would draw a ring at every joint.
if (BA.y < -0.5)
{
    float2 q = float2(max(abs(L.x) - max(H.x - H.y, 0.0), 0.0), L.y);
    return float4(c, saturate(H.y - length(q) + 0.5) * Opacity);
}
float2 d = H - abs(L);
float cov = saturate(d.x + 0.5) * saturate(d.y + 0.5);
if (Outlines > 0.5 && min(H.x, H.y) > 2.5) { c *= lerp(0.6, 1.0, saturate(min(d.x, d.y) / 1.25)); }
return float4(c, cov * Opacity);
"""

y = [0]


def node(cls, **props):
    n = mel.create_material_expression(mat, cls, -600, y[0])
    y[0] += 160
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def uv(index):
    return node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=index)


def param(name, value):
    return node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)


def custom(desc, code, output, sources):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -200, y[0])
    y[0] += 240
    n.set_editor_property("description", desc)
    n.set_editor_property("code", code)
    n.set_editor_property("output_type", output)
    inputs = []
    for name in sources:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    n.set_editor_property("inputs", inputs)
    for name, source in sources.items():
        if not mel.connect_material_expressions(source, "", n, name):
            unreal.log_error(f"ATLAS: failed to wire {name} on {desc}")
    return n


Out = unreal.CustomMaterialOutputType
scale, offset, corner, a, b = uv(0), uv(1), uv(4), uv(2), uv(3)
zmin, zmax = param("ZMin", 0.0), param("ZMax", 16383.0)
paint_mode = param("PaintMode", 0.0)
outlines = param("Outlines", 1.0)
opacity = param("Opacity", 1.0)
palette = node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="Palette",
               texture=unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
vs = {"S": scale, "O": offset, "C": corner, "A": a, "B": b, "ZMin": zmin, "ZMax": zmax, "PaintMode": paint_mode, "Pal": palette}

position = custom("Instance to window position", CODE_POSITION, Out.CMOT_FLOAT3, vs)
local = custom("Pixel offset from centre", CODE_LOCAL, Out.CMOT_FLOAT2, vs)
half = custom("Half size in pixels", CODE_HALF, Out.CMOT_FLOAT2, vs)
color_rg = custom("Colour red and green", CODE_COLOR_RG, Out.CMOT_FLOAT2, vs)
color_b = custom("Colour blue", CODE_COLOR_B, Out.CMOT_FLOAT2, vs)

ps_local, ps_half, ps_rg, ps_b = uv(0), uv(1), uv(2), uv(3)
color = custom("Colour and coverage", CODE_COLOR, Out.CMOT_FLOAT4, {"L": ps_local, "H": ps_half, "RG": ps_rg, "BA": ps_b, "Outlines": outlines, "Opacity": opacity})
rgb = node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=True, a=False)
alpha = node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=False, a=True)
mel.connect_material_expressions(color, "", rgb, "")
mel.connect_material_expressions(color, "", alpha, "")

Prop = unreal.MaterialProperty
tools_cls = unreal.AtlasEditorTools
ok = [
    mel.connect_material_property(rgb, "", Prop.MP_EMISSIVE_COLOR),
    mel.connect_material_property(alpha, "", Prop.MP_OPACITY),
    mel.connect_material_property(position, "", Prop.MP_WORLD_POSITION_OFFSET),
    tools_cls.connect_customized_uv(mat, local, 0),
    tools_cls.connect_customized_uv(mat, half, 1),
    tools_cls.connect_customized_uv(mat, color_rg, 2),
    tools_cls.connect_customized_uv(mat, color_b, 3),
]
unreal.log(f"ATLAS: connected {ok}")

mel.recompile_material(mat)
lib.save_loaded_asset(mat, only_if_is_dirty=False)
unreal.log("ATLAS: material saved")
