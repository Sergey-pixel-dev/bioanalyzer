#!/usr/bin/env python3
"""Protocol 2 JSON-lines worker for arbitrary model families."""
import argparse, importlib, json, os, sys

PROTOCOL = 2

def emit(obj):
    sys.stdout.write(json.dumps({"protocol": PROTOCOL, **obj}, ensure_ascii=False) + "\n")
    sys.stdout.flush()

def load_family(name, models_dir=None):
    if models_dir and models_dir not in sys.path:
        sys.path.insert(0, models_dir)
    try:
        return importlib.import_module(f"models.{name}")
    except Exception as exc:
        emit({"type": "error", "message": f"Cannot load model family '{name}': {exc}"})
        return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", required=True, choices=("describe", "train", "infer"))
    ap.add_argument("--dataset"); ap.add_argument("--output"); ap.add_argument("--model")
    ap.add_argument("--family", required=True); ap.add_argument("--models-dir")
    ap.add_argument("--config", default="{}")
    args = ap.parse_args()
    family = load_family(args.family, args.models_dir)
    if family is None: return 2
    if args.mode == "describe":
        if not hasattr(family, "meta"):
            emit({"type":"error", "message":"Model family does not export meta()"}); return 2
        try: emit({"type":"meta", "family":args.family, "meta":family.meta()}); return 0
        except Exception as exc: emit({"type":"error", "message":str(exc)}); return 1
    if args.mode == "train":
        if not args.dataset or not args.output: emit({"type":"error","message":"Dataset and output are required"}); return 2
        try:
            config = json.loads(args.config)
            def report(epoch,total,train_loss,val_loss=0.0,accuracy=0.0,**extra):
                emit({"type":"metric","epoch":int(epoch),"epochs":int(total),"train_loss":float(train_loss),"val_loss":float(val_loss),"accuracy":float(accuracy),"value":float(epoch)/max(1,total),**extra})
            result = family.train(args.dataset, args.output, report, config)
            emit({"type":"result","output":{"kind":"model","path":args.output,"metrics":result or {}}}); return 0
        except Exception as exc: emit({"type":"error","message":str(exc)}); return 1
    if not args.model: emit({"type":"error","message":"Model bundle is required"}); return 2
    try: model = family.load(args.model)
    except Exception as exc: emit({"type":"error","message":str(exc)}); return 1
    for line in sys.stdin:
        try:
            cmd=json.loads(line)
            if cmd.get("cmd")=="stop": return 0
            if cmd.get("cmd")=="window":
                output=family.predict(model,cmd.get("values",[]),int(cmd.get("rows",0)),int(cmd.get("channels",0)))
                emit({"type":"result","output":output if isinstance(output,dict) else {"kind":"top_k","items":output}})
        except Exception as exc: emit({"type":"error","message":str(exc)})
    return 0

if __name__ == "__main__": sys.exit(main())
