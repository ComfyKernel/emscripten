//
// WebAssembly representation and processing library
//

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace wasm {

// Utilities

// Arena allocation for mixed-type data.
struct Arena {
  std::vector<char*> chunks;
  int index; // in last chunk

  template<class T>
  T* alloc() {
    const size_t CHUNK = 10000;
    size_t currSize = (sizeof(T) + 7) & (-8); // same alignment as malloc TODO optimize?
    assert(currSize < CHUNK);
    if (chunks.size() == 0 || index + currSize >= CHUNK) {
      chunks.push_back(new char[CHUNK]);
      index = 0;
    }
    T* ret = (T*)(chunks.back() + index);
    index += currSize;
    new (ret) T();
    return ret;
  }

  void clear() {
    for (char* chunk : chunks) {
      delete[] chunk;
    }
    chunks.clear();
  }

  ~Arena() {
    clear();
  }
};

std::ostream &doIndent(std::ostream &o, unsigned indent) {
  for (unsigned i = 0; i < indent; i++) {
    o << "  ";
  }
  return o;
}
void incIndent(std::ostream &o, unsigned& indent) {
  o << '\n';
  indent++;    
}
void decIndent(std::ostream &o, unsigned& indent) {
  indent--;
  doIndent(o, indent);
  o << ')';
}

// Basics

typedef cashew::IString Name;

// A 'var' in the spec.
class Var {
  enum {
    MAX_NUM = 1000000 // less than this, a num, higher, a string; 0 = null
  };
  union {
    unsigned num; // numeric ID
    Name str;     // string
  };
public:
  Var() : num(0) {}
  Var(unsigned num) : num(num) {
    assert(num > 0 && num < MAX_NUM);
  }
  Var(Name str) : str(str) {
    assert(num > MAX_NUM);
  }

  bool is() {
    return num != 0;
  }

  std::ostream& print(std::ostream &o) {
    if (num < MAX_NUM) {
      o << num;
    } else {
      o << str.str;
    }
    return o;
  }
};

// Types

enum BasicType {
  none,
  i32,
  i64,
  f32,
  f64
};

std::ostream& printBasicType(std::ostream &o, BasicType type) {
  switch (type) {
    case BasicType::none: o << "none"; break;
    case BasicType::i32: o << "i32"; break;
    case BasicType::i64: o << "i64"; break;
    case BasicType::f32: o << "f32"; break;
    case BasicType::f64: o << "f64"; break;
  }
  return o;
}

unsigned getBasicTypeSize(BasicType type) {
  switch (type) {
    case BasicType::none: abort();
    case BasicType::i32: return 4;
    case BasicType::i64: return 8;
    case BasicType::f32: return 4;
    case BasicType::f64: return 8;
  }
}

struct Literal {
  BasicType type;
  union {
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
  };

  Literal() : type(BasicType::none) {}
  Literal(int32_t init) : type(BasicType::i32), i32(init) {}
  Literal(int64_t init) : type(BasicType::i64), i64(init) {}
  Literal(float   init) : type(BasicType::f32), f32(init) {}
  Literal(double  init) : type(BasicType::f64), f64(init) {}

  std::ostream& print(std::ostream &o) {
    switch (type) {
      case none: abort();
      case BasicType::i32: o << i32; break;
      case BasicType::i64: o << i64; break;
      case BasicType::f32: o << f32; break;
      case BasicType::f64: o << f64; break;
    }
    return o;
  }
};

// Operators

enum UnaryOp {
  Clz, Ctz, Popcnt, // int
  Neg, Abs, Ceil, Floor, Trunc, Nearest, Sqrt // float
};

enum BinaryOp {
  Add, Sub, Mul, // int or float
  DivS, DivU, RemS, RemU, And, Or, Xor, Shl, ShrU, ShrS, // int
  Div, CopySign, Min, Max // float
};

enum RelationalOp {
  Eq, Ne, // int or float
  LtS, LtU, LeS, LeU, GtS, GtU, GeS, GeU, // int
  Lt, Le, Gt, Ge // float
};

enum ConvertOp {
  ExtendSInt32, ExtendUInt32, WrapInt64, TruncSFloat32, TruncUFloat32, TruncSFloat64, TruncUFloat64, ReinterpretFloat, // int
  ConvertSInt32, ConvertUInt32, ConvertSInt64, ConvertUInt64, PromoteFloat32, DemoteFloat64, ReinterpretInt // float
};

enum HostOp {
  PageSize, MemorySize, GrowMemory, HasFeature
};

// Expressions

class Expression {
public:
  virtual std::ostream& print(std::ostream &o, unsigned indent) = 0;

  template<class T>
  bool is() {
    return !!dynamic_cast<T*>(this);
  }
};

std::ostream& printFullLine(std::ostream &o, unsigned indent, Expression *expression) {
  doIndent(o, indent);
  expression->print(o, indent);
  o << '\n';
}

typedef std::vector<Expression*> ExpressionList; // TODO: optimize  

class Nop : public Expression {
  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "nop";
    return o;
  }
};

class Block : public Expression {
public:
  Var var;
  ExpressionList list;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(block";
    if (var.is()) {
      o << " ";
      var.print(o);
    }
    incIndent(o, indent);
    for (auto expression : list) {
      printFullLine(o, indent, expression);
    }
    decIndent(o, indent);
    return o;
  }
};

class If : public Expression {
public:
  Expression *condition, *ifTrue, *ifFalse;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(if";
    incIndent(o, indent);
    printFullLine(o, indent, condition);
    printFullLine(o, indent, ifTrue);
    if (ifFalse) printFullLine(o, indent, ifFalse);
    decIndent(o, indent);
    return o;
  }
};

class Loop : public Expression {
public:
  Var out, in;
  Expression *body;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(loop";
    if (out.is()) {
      o << " ";
      out.print(o);
      if (in.is()) {
        o << " ";
        in.print(o);
      }
    }
    incIndent(o, indent);
    printFullLine(o, indent, body);
    decIndent(o, indent);
    return o;
  }
};

class Label : public Expression {
public:
  Var var;
};

class Break : public Expression {
public:
  Var var;
  Expression *condition, *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(break ";
    var.print(o);
    incIndent(o, indent);
    if (condition) printFullLine(o, indent, condition);
    if (value) printFullLine(o, indent, value);
    decIndent(o, indent);
    return o;
  }
};

class Switch : public Expression {
public:
  struct Case {
    Literal value;
    Expression *body;
    bool fallthru;
  };

  Var var;
  Expression *value;
  std::vector<Case> cases;
  Expression *default_;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(switch ";
    var.print(o);
    incIndent(o, indent);
    printFullLine(o, indent, value);
    o << "TODO: cases/default\n";
    decIndent(o, indent);
    return o;
  }

};

class Call : public Expression {
public:
  Var target;
  ExpressionList operands;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(call ";
    target.print(o);
    incIndent(o, indent);
    for (auto operand : operands) {
      printFullLine(o, indent, operand);
    }
    decIndent(o, indent);
    return o;
  }
};

class CallImport : public Call {
};

class CallIndirect : public Expression {
public:
  Expression *target;
  ExpressionList operands;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(callindirect ";
    incIndent(o, indent);
    printFullLine(o, indent, target);
    for (auto operand : operands) {
      printFullLine(o, indent, operand);
    }
    decIndent(o, indent);
    return o;
  }
};

class GetLocal : public Expression {
public:
  Var id;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(setlocal ";
    id.print(o) << ')';
    return o;
  }
};

class SetLocal : public Expression {
public:
  Var id;
  Expression *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(setlocal ";
    id.print(o);
    incIndent(o, indent);
    printFullLine(o, indent, value);
    decIndent(o, indent);
    return o;
  }
};

class Load : public Expression {
public:
  unsigned bytes;
  bool signed_;
  int offset;
  unsigned align;
  Expression *ptr;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(load " << bytes << ' ' << signed_ << ' ' << offset << ' ' << align;
    incIndent(o, indent);
    printFullLine(o, indent, ptr);
    decIndent(o, indent);
    return o;
  }
};

class Store : public Expression {
public:
  unsigned bytes;
  int offset;
  unsigned align;
  Expression *ptr, *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(load " << bytes << ' ' << ' ' << offset << ' ' << align;
    incIndent(o, indent);
    printFullLine(o, indent, ptr);
    printFullLine(o, indent, value);
    decIndent(o, indent);
    return o;
  }
};

class Const : public Expression {
public:
  Literal value;

  Const* set(Literal value_) {
    value = value_;
    return this;
  }

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(literal ";
    value.print(o);
    o << ')';
  }
};

class Unary : public Expression {
public:
  UnaryOp op;
  Expression *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(unary ";
    switch (op) {
      case Neg: o << "neg"; break;
      default: abort();
    }
    incIndent(o, indent);
    printFullLine(o, indent, value);
    decIndent(o, indent);
    return o;
  }
};

class Binary : public Expression {
public:
  BinaryOp op;
  Expression *left, *right;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(binary ";
    switch (op) {
      case Add:      o << "add"; break;
      case Sub:      o << "sub"; break;
      case Mul:      o << "mul"; break;
      case DivS:     o << "divs"; break;
      case DivU:     o << "divu"; break;
      case RemS:     o << "rems"; break;
      case RemU:     o << "remu"; break;
      case And:      o << "and"; break;
      case Or:       o << "or"; break;
      case Xor:      o << "xor"; break;
      case Shl:      o << "shl"; break;
      case ShrU:     o << "shru"; break;
      case ShrS:     o << "shrs"; break;
      case Div:      o << "div"; break;
      case CopySign: o << "copysign"; break;
      case Min:      o << "min"; break;
      case Max:      o << "max"; break;
      default: abort();
    }
    incIndent(o, indent);
    printFullLine(o, indent, left);
    printFullLine(o, indent, right);
    decIndent(o, indent);
    return o;
  }
};

class Compare : public Expression {
public:
  RelationalOp op;
  Expression *left, *right;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(compare ";
    switch (op) {
      case Eq:  o << "eq"; break;
      case Ne:  o << "ne"; break;
      case LtS: o << "lts"; break;
      case LtU: o << "ltu"; break;
      case LeS: o << "les"; break;
      case LeU: o << "leu"; break;
      case GtS: o << "gts"; break;
      case GtU: o << "gtu"; break;
      case GeS: o << "ges"; break;
      case GeU: o << "geu"; break;
      case Lt:  o << "lt"; break;
      case Le:  o << "le"; break;
      case Gt:  o << "gt"; break;
      case Ge:  o << "ge"; break;
      default: abort();
    }
    incIndent(o, indent);
    printFullLine(o, indent, left);
    printFullLine(o, indent, right);
    decIndent(o, indent);
    return o;
  }
};

class Convert : public Expression {
public:
  ConvertOp op;
  Expression *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "(convert ";
    switch (op) {
      case ConvertUInt32: o << "uint32toDouble"; break;
      case ConvertSInt32: o << "sint32toDouble"; break;
      case TruncSFloat64: o << "float64tosint32"; break;
      default: abort();
    }
    incIndent(o, indent);
    printFullLine(o, indent, value);
    decIndent(o, indent);
    return o;
  }
};

class Host : public Expression {
public:
  HostOp op;
  ExpressionList operands;
};

// Globals

struct NameType {
  Name name;
  BasicType type;
  NameType() : name(nullptr), type(none) {}
  NameType(Name name, BasicType type) : name(name), type(type) {}
};

std::ostream& printParamsAndResult(std::ostream &o, unsigned indent, BasicType result, std::vector<NameType>& params) {
  for (auto& param : params) {
    o << "(param " << param.name.str << " ";
    printBasicType(o, param.type) << ") ";
  }
  o << "(result ";
  printBasicType(o, result) << ")";
}

class FunctionType {
public:
  Name name;
  BasicType result;
  std::vector<BasicType> params;

  std::ostream& print(std::ostream &o, unsigned indent) {
    o << "(type " << name.str;
    incIndent(o, indent);
    doIndent(o, indent);
    for (auto& param : params) {
      o << "(param ";
      printBasicType(o, param) << ") ";
    }
    o << "(result ";
    printBasicType(o, result) << ")\n";
    decIndent(o, indent);
    return o;
  }

  bool operator==(FunctionType& b) {
    if (name != b.name) return false; // XXX
    if (result != b.result) return false;
    if (params.size() != b.params.size()) return false;
    for (size_t i = 0; i < params.size(); i++) {
      if (params[i] != b.params[i]) return false;
    }
    return true;
  }
  bool operator!=(FunctionType& b) {
    return !(*this == b);
  }
};

class Function {
public:
  Name name;
  BasicType result;
  std::vector<NameType> params;
  std::vector<NameType> locals;
  Expression *body;

  std::ostream& print(std::ostream &o, unsigned indent) {
    o << "(func " << name.str << " ";
    printParamsAndResult(o, indent, result, params);
    incIndent(o, indent);
    for (auto& local : locals) {
      doIndent(o, indent);
      o << "(local " << local.name.str << " ";
      printBasicType(o, local.type) << ")\n";
    }
    printFullLine(o, indent, body);
    decIndent(o, indent);
    return o;
  }
};

class Import {
public:
  Name name, module, base; // name = module.base
  FunctionType type;

  std::ostream& print(std::ostream &o, unsigned indent) {
    o << "(import " << name.str << " \"" << module.str << "\" \"" << base.str << "\" ";
    type.print(o, indent);
    o << ')';
    return o;
  }
};

class Export {
public:
  Name name;
  Var value;

  std::ostream& print(std::ostream &o, unsigned indent) {
    o << "(export \"" << name.str << "\" ";
    value.print(o);
    o << ')';
    return o;
  }
};

class Table {
public:
  std::vector<Var> vars;

  std::ostream& print(std::ostream &o, unsigned indent) {
    o << "(table ";
    for (auto var : vars) {
      var.print(o) << ' ';
    }
    o << ')';
    return o;
  }
};

class Module {
protected:
  // wasm contents
  std::vector<FunctionType> functionTypes;
  std::map<Name, Import> imports;
  std::vector<Export> exports;
  Table table;
  std::vector<Function*> functions;

  // internals
  std::map<Var, void*> map; // maps var ids/names to things
  unsigned nextVar;

public:
  Module() : nextVar(1) {}

  std::ostream& print(std::ostream &o) {
    unsigned indent = 0;
    o << "(module";
    incIndent(o, indent);
    for (auto& curr : functionTypes) {
      doIndent(o, indent);
      curr.print(o, indent);
      o << '\n';
    }
    for (auto& curr : imports) {
      doIndent(o, indent);
      curr.second.print(o, indent);
      o << '\n';
    }
    for (auto& curr : exports) {
      doIndent(o, indent);
      curr.print(o, indent);
      o << '\n';
    }
    doIndent(o, indent);
    table.print(o, indent);
    o << '\n';
    for (auto& curr : functions) {
      doIndent(o, indent);
      curr->print(o, indent);
      o << '\n';
    }
    decIndent(o, indent);
    o << '\n';
  }
};

} // namespace wasm
