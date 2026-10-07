"""Data-only profile systems. No eval, imports or manufacturer-specific formulas."""
import ast
from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path


def dimension(expression, variables):
    if isinstance(expression, bool):
        raise ValueError('尺寸规则不能是布尔值')
    if isinstance(expression, (int,float)):
        result = float(expression)
    elif isinstance(expression,str) and len(expression)<=512:
        tree=ast.parse(expression,mode='eval')
        if sum(1 for _ in ast.walk(tree))>100:
            raise ValueError('尺寸表达式过于复杂')
        def visit(node):
            if isinstance(node,ast.Expression): return visit(node.body)
            if isinstance(node,ast.Constant) and type(node.value) in (int,float): return float(node.value)
            if isinstance(node,ast.Name) and node.id in variables: return float(variables[node.id])
            if isinstance(node,ast.UnaryOp) and isinstance(node.op,(ast.UAdd,ast.USub)):
                return visit(node.operand)*(1 if isinstance(node.op,ast.UAdd) else -1)
            if isinstance(node,ast.BinOp) and isinstance(node.op,(ast.Add,ast.Sub,ast.Mult,ast.Div)):
                a,b=visit(node.left),visit(node.right)
                if isinstance(node.op,ast.Add): return a+b
                if isinstance(node.op,ast.Sub): return a-b
                if isinstance(node.op,ast.Mult): return a*b
                if b==0: raise ValueError('尺寸规则除数为零')
                return a/b
            raise ValueError('尺寸规则仅允许数值、已声明变量和加减乘除')
        result=visit(tree)
    else:
        raise ValueError('尺寸规则格式无效')
    if not math.isfinite(result) or abs(result)>1e8:
        raise ValueError('尺寸规则结果无效')
    return result


class ProfileSystem:
    def __init__(self, definition, digest=''):
        self.data=deepcopy(definition)
        d=self.data
        if d.get('schema')!='icax.window-profile-system' or d.get('schemaVersion')!=1:
            raise ValueError('不支持的铝窗型材系统格式')
        if d.get('units')!='mm':raise ValueError('型材系列必须明确使用毫米单位')
        for key in ('id','series','revision','profiles','roles','dimensions','joints','compatibility'):
            if not d.get(key): raise ValueError('型材系统缺少 '+key)
        self.digest=digest
        self.ready=d.get('status')=='verified' and bool(d.get('manufacturer')) and bool(d.get('source'))
        self.constants={key:dimension(value,{}) for key,value in d.get('constants',{}).items()}
        for name,profile in d['profiles'].items():
            if not profile.get('contours') or not isinstance(profile['contours'],list):
                raise ValueError(name+' 缺少实际材料轮廓')
            if any(not isinstance(c,dict) or 'kind' not in c for c in profile['contours']):
                raise ValueError(name+' 轮廓无效')
            if len(profile.get('referenceOrigin',[]))!=2:
                raise ValueError(name+' 缺少二维装配基准')
            if any(not math.isfinite(float(v)) for v in profile['referenceOrigin']):
                raise ValueError('装配基准无效')
            profile['face']=dimension(profile.get('face',0),{})
            profile['depth']=dimension(profile.get('depth',0),{})
            if profile['face']<=0 or profile['depth']<=0:
                raise ValueError(name+' 缺少装配占位尺寸')
            if not isinstance(profile.get('material'),str) or not profile['material'].strip():
                raise ValueError(name+' 缺少材质')
            if any(v not in d['profiles'] for v in profile.get('compatibleProfiles',[])):
                raise ValueError(name+' 声明了不存在的兼容型材')
            dimension(profile.get('referenceAngle',0),{})
        for role,profile_id in d['roles'].items():
            if profile_id not in d['profiles']: raise ValueError(role+' 指向不存在的型材')
        for group,mode in d['joints'].items():
            if mode not in ('miter','side_wrap','horizontal_wrap'):
                raise ValueError(group+' 拼角规则无效')

    def profile(self,role):
        try:
            ident=self.data['roles'][role]
            return ident,self.data['profiles'][ident]
        except KeyError as e:
            raise ValueError('型材系统缺少角色 '+role) from e

    def rule(self,name,variables):
        if name not in self.data['dimensions']:
            raise ValueError('型材系统缺少尺寸规则 '+name)
        return dimension(self.data['dimensions'][name],{**self.constants,**variables})

    def compatible(self,family):
        if family not in self.data['compatibility'].get('panels',[]):
            raise ValueError('当前型材系列不支持 '+family)

    def compatible_profiles(self,first,second):
        a,pa=self.profile(first)
        b,pb=self.profile(second)
        if b not in pa.get('compatibleProfiles',[]) or a not in pb.get('compatibleProfiles',[]):
            raise ValueError('型材连接未获系列规则允许：'+first+' / '+second)

    def track_layout(self,tracks,count,screen):
        key=f'{tracks}/{count}/'+('screen' if screen else 'glass')
        layout=self.data.get('trackLayouts',{}).get(key)
        if not isinstance(layout,dict):raise ValueError('型材系列没有声明该扇数和轨道组合：'+key)
        glass=layout.get('glass',[])
        screen_track=layout.get('screen')
        if len(glass)!=count or any(type(v) is not int or not 0<=v<tracks for v in glass):
            raise ValueError('玻璃扇轨道分配无效')
        if screen and (type(screen_track) is not int or not 0<=screen_track<tracks or screen_track in glass):
            raise ValueError('纱窗必须分配独立轨道')
        return glass,screen_track


def load_system(parameters):
    source=parameters['systemSource']
    if source=='demonstration':
        path=Path(__file__).with_name('systems')/'demonstration.json'
    elif source=='file':
        path=Path(parameters['systemFile'])
        if not path.is_absolute() or path.suffix.lower()!='.json':
            raise ValueError('请选择绝对路径的型材系统 JSON 文件')
    else:
        raise ValueError('型材系统来源无效')
    if path.stat().st_size>2_000_000:
        raise ValueError('型材系统文件超过2MB')
    raw=path.read_bytes()
    data=json.loads(raw.decode('utf-8-sig'))
    return ProfileSystem(data,hashlib.sha256(raw).hexdigest())
