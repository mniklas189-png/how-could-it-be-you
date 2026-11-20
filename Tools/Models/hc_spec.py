"""The list of models MakeModels.py knows: one entry per build function, filled by the @model decorator."""

MODELS = {}


class Spec:
    def __init__(self, name, size_cm, build, tex, tris, glow, glow_strength, glass, pivot, ground, sharp, note):
        self.name = name
        self.size_cm = size_cm            # target size X, Y, Z in centimetres
        self.build = build                # function(builder)
        self.tex = tex                    # texture edge length in pixels
        self.tris = tris                  # triangle budget
        self.glow = glow                  # linear colour of the Glow slot, or None
        self.glow_strength = glow_strength
        self.glass = glass                # linear colour of the Glass slot, or None
        self.pivot = pivot                # 'bottom' (bottom centre of the bounds) or 'hinge' (VentGrille)
        self.ground = ground              # the floor shades the model in the bake
        self.sharp = sharp                # edges sharper than this many degrees stay hard
        self.note = note


def model(name, size_cm, tex=1024, tris=8000, glow=None, glow_strength=None, glass=None, pivot='bottom',
          ground=True, sharp=50.0, note=None):
    def register(build):
        MODELS[name] = Spec(name, size_cm, build, tex, tris, glow, glow_strength, glass, pivot, ground, sharp, note)
        return build
    return register
