import importlib.util
import json
from pathlib import Path
from icax_template_sdk import NeutralModel

ROOT=next(p for p in Path(__file__).resolve().parents if (p/"src/apps").is_dir())
spec=importlib.util.spec_from_file_location("market_profile_runtime",ROOT/"src/apps/tube-designer/templates/_shared/profile_package_runtime.py")
runtime=importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)

def generate(parameters,context):
    root=Path(parameters["profileRoot"])
    package=runtime._system_package(root/parameters["profileId"],preview=False)
    values=package["defaultParameters"].copy()
    scale=parameters.get("scale",1.0)
    for definition in package["descriptor"]["parameters"]:
        key=definition["key"]
        if definition["valueType"]=="number" and "°" not in definition["displayName"]:
            values[key]*=scale
    model=NeutralModel(template_id="market-profile-probe",template_version="1.0.0",package_digest="test",parameters=parameters)
    cases=[values]
    for definition in package["descriptor"]["parameters"]:
        if definition["key"] in ("sectionModel","manufacturingRoute"):
            cases=[dict(values,**{definition["key"]:option["value"]}) for option in definition["options"]]
    if "mirrorX" in values:cases += [dict(v,mirrorX=True) for v in list(cases)]
    if parameters["profileId"]=="u-section":
        cases += [dict(values,bottomWidth=values['width']),dict(values,bendRadius=0),
                  dict(values,width=400*scale,bottomWidth=120*scale,depth=220*scale)]
    if parameters["profileId"]=="rect":
        corners=json.dumps([{"mode":"sharp"},{"mode":"arc","radius":3*scale},{"mode":"chamfer","incoming":3*scale,"outgoing":3*scale},{"mode":"custom","incoming":3*scale,"outgoing":3*scale}])
        cases.append(dict(values,outerCorners=corners))
    items=[]
    for index,case in enumerate(cases):
        profile=runtime._evaluate(package["descriptor"],package["scriptSource"],case,
            package["packageDigest"],package["sourceFileName"],package["resources"])
        face=model.geometry(f"section{index}","profile2d",arguments={"contours":profile["contours"],
            "placement":{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]}})
        solid=model.geometry(f"stock{index}","extrude",inputs=[face],arguments={"vector":[0,0,250]})
        items.append(model.item(f"stock.{index}",profile["name"],representations={"display":solid,"export":solid}))
    model.output("display.default","display",items)
    return model.build()
