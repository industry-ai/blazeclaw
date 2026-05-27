import onnx
from pathlib import Path
root = Path('BlazeClawMfc/models/STT/sherpa-onnx-streaming-zipformer-bilingual-zh-en')
models = ['encoder-epoch-99-avg-1.int8.onnx', 'decoder-epoch-99-avg-1.int8.onnx', 'joiner-epoch-99-avg-1.int8.onnx']
for name in models:
    m = onnx.load(str(root / name))
    print('\n' + name)
    for i in m.graph.input:
        dims = [d.dim_param or d.dim_value for d in i.type.tensor_type.shape.dim]
        print(' in', i.name, dims, i.type.tensor_type.elem_type)
    for o in m.graph.output:
        dims = [d.dim_param or d.dim_value for d in o.type.tensor_type.shape.dim]
        print(' out', o.name, dims, o.type.tensor_type.elem_type)
