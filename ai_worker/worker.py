#!/usr/bin/env python3
"""Versioned JSON-lines worker. Model implementations live in models/<family>.py."""
import argparse, importlib, json, os, sys, time

PROTOCOL = 1
def emit(obj):
    obj = {"protocol": PROTOCOL, **obj}
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n"); sys.stdout.flush()

def load_family(name):
    try:
        return importlib.import_module(f"models.{name}")
    except Exception as e:
        emit({"type":"error", "message":f"Cannot load model family '{name}': {e}"})
        return None

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--mode",required=True); ap.add_argument("--dataset"); ap.add_argument("--output"); ap.add_argument("--model"); ap.add_argument("--family",default="custom"); ap.add_argument("--epochs",type=int,default=20); ap.add_argument("--batch-size",type=int,default=32); ap.add_argument("--learning-rate",type=float,default=1e-3); ap.add_argument("--validation-split",type=float,default=.2); ap.add_argument("--seed",type=int,default=1); ap.add_argument("--device",default="cuda")
    a=ap.parse_args(); emit({"type":"hello","worker":"ai_worker","mode":a.mode})
    family = load_family(a.family)
    if family is None: return 2
    if a.mode == "train":
        if not a.dataset or not a.output: emit({"type":"error","message":"Dataset and output bundle are required"}); return 2
        try:
            def report(epoch, total, train_loss, val_loss=0.0, accuracy=0.0):
                emit({"type":"metric","epoch":int(epoch),"epochs":int(total),"train_loss":float(train_loss),"val_loss":float(val_loss),"accuracy":float(accuracy),"value":float(epoch)/max(1,total)})
            config = {"epochs":a.epochs,"batch_size":a.batch_size,"learning_rate":a.learning_rate,"validation_split":a.validation_split,"seed":a.seed,"device":a.device}
            try: result = family.train(a.dataset, a.output, report, config)
            except TypeError: result = family.train(a.dataset, a.output, report)
            emit({"type":"result","kind":"model","path":a.output,"metrics":result or {}}); return 0
        except Exception as e:
            emit({"type":"error","message":str(e)}); return 1
    if a.mode == "infer":
        try: model = family.load(a.model)
        except Exception as e: emit({"type":"error","message":str(e)}); return 1
        for line in sys.stdin:
            try:
                cmd=json.loads(line)
                if cmd.get("cmd")=="stop": return 0
                if cmd.get("cmd")=="window":
                    top5=family.predict(model, cmd.get("values",[]), cmd.get("rows",0), cmd.get("channels",0))
                    emit({"type":"result","top5":top5})
            except Exception as e: emit({"type":"error","message":str(e)})
        return 0
    emit({"type":"error","message":"Unsupported mode"}); return 2
if __name__ == "__main__": sys.exit(main())
