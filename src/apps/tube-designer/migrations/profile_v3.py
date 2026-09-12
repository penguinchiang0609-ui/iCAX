"""Offline profile definition migration. Never imported by the production runtime.

Only standalone profile.json files are supported. Drawing databases, snapshots
and pinned package references require a separate transactional migration.
Writes a new file with exclusive creation; the original remains untouched.
"""
import argparse
import copy
import json
from pathlib import Path

def migrate_definition(value):
    result=copy.deepcopy(value)
    if result.get("schema")!="icax.tube-profile-descriptor":
        raise ValueError("只支持独立 profile.json；不支持图纸或包记录")
    if result.get("schemaVersion")==3:
        if result.get("profileForm") not in ("parametric","fixed"):
            raise ValueError("新版定义缺少合法 profileForm")
        return result
    if result.get("schemaVersion")!=2:
        raise ValueError("不支持此历史版本")
    form=result.get("profileForm")
    if form is None:
        if isinstance(result.get("parameters"),list) and result["parameters"]:
            form="parametric"
        else:
            raise ValueError("不能确定形式，需要人工确认")
    if form not in ("parametric","fixed"):
        raise ValueError("管型形式无效")
    if form=="fixed" and (result.get("parameters") or not isinstance(result.get("section"),dict)):
        raise ValueError("定式缺少固定截面或仍声明参数")
    result.update(schemaVersion=3,profileForm=form)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source",type=Path)
    parser.add_argument("--output",type=Path,help="新文件路径；不指定时只校验并打印，不写文件")
    args=parser.parse_args()
    result=migrate_definition(json.loads(args.source.read_text(encoding="utf-8-sig")))
    text=json.dumps(result,ensure_ascii=False,indent=2)+"\n"
    if args.output:
        if args.output.resolve()==args.source.resolve():
            raise ValueError("禁止覆盖原文件")
        with args.output.open("x",encoding="utf-8") as stream:stream.write(text)
    else:print(text)

if __name__=="__main__":main()
