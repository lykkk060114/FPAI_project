from pathlib import Path
import json
from tokenizers import Tokenizer
r=Path(__file__).resolve().parents[1];t=Tokenizer.from_file(str(r/'runtime/tokenizer.json'))
chat='<|im_start|>system\n你是熊猫行为记录助手，只描述数据，不作健康诊断。<|im_end|>\n<|im_start|>user\n观察300秒，进食120秒，休息150秒，行走20秒，喝水10秒。请用一句中文总结。<|im_end|>\n<|im_start|>assistant\n'
ids=t.encode(chat,add_special_tokens=False).ids
(r/'results/behavior_prompt.ids').write_text('\n'.join(map(str,ids))+'\n');(r/'results/behavior_prompt.txt').write_text(chat);print('prompt tokens',len(ids))
