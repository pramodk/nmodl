#include "pybind/pyast.hpp"
#include "visitors/json_visitor.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"
{% macro var(node) -%}
{{ node.class_name | snake_case }}_
{%- endmacro -%}

{% macro args(children) %}
{% for c in children %} {{ c.get_typename() }} {%- if not loop.last %}, {% endif %} {% endfor %}
{%- endmacro -%}

namespace py = pybind11;
using namespace ast;

void init_ast_module(py::module& m) {

    py::module m_ast = m.def_submodule("ast");

    py::enum_<BinaryOp>(m_ast, "BinaryOp")
            .value("BOP_ADDITION", BinaryOp::BOP_ADDITION)
            .value("BOP_SUBTRACTION", BinaryOp::BOP_SUBTRACTION)
            .value("BOP_MULTIPLICATION", BinaryOp::BOP_MULTIPLICATION)
            .value("BOP_DIVISION", BinaryOp::BOP_DIVISION)
            .value("BOP_POWER", BinaryOp::BOP_POWER)
            .value("BOP_AND", BinaryOp::BOP_AND)
            .value("BOP_OR", BinaryOp::BOP_OR)
            .value("BOP_GREATER", BinaryOp::BOP_GREATER)
            .value("BOP_LESS", BinaryOp::BOP_LESS)
            .value("BOP_GREATER_EQUAL", BinaryOp::BOP_GREATER_EQUAL)
            .value("BOP_LESS_EQUAL", BinaryOp::BOP_LESS_EQUAL)
            .value("BOP_ASSIGN", BinaryOp::BOP_ASSIGN)
            .value("BOP_NOT_EQUAL", BinaryOp::BOP_NOT_EQUAL)
            .value("BOP_EXACT_EQUAL", BinaryOp::BOP_EXACT_EQUAL)
            .export_values();

    py::enum_<AstNodeType>(m_ast, "AstNodeType")
    {% for node in nodes %}
            .value("{{ node.class_name|snake_case|upper }}", AstNodeType::{{ node.class_name|snake_case|upper }})
    {% endfor %}
            .export_values();



    py::class_<AST, PyAST> ast_(m_ast, "AST");
    ast_.def(py::init<>())
            .def("visit_children", &AST::visit_children)
            .def("accept", &AST::accept)
            .def("get_node_type", &AST::get_node_type)
            .def("get_node_type_name", &AST::get_node_type_name)
            .def("get_node_name", &AST::get_node_name)
            .def("clone", &AST::clone)
            .def("get_token", &AST::get_token)
            .def("set_symbol_table", &AST::set_symbol_table)
            .def("get_symbol_table", &AST::get_symbol_table)
            .def("get_statement_block", &AST::get_statement_block)
            .def("negate", &AST::negate)
            .def("set_name", &AST::set_name)
            .def("is_ast", &AST::is_ast)
    {% for node in nodes %}
    .def("is_{{ node.class_name | snake_case }}", &AST::is_{{ node.class_name | snake_case }})
    {% if loop.last -%};{% endif %}
    {% endfor %}

    {% for node in nodes %}
    py::class_<{{ node.class_name }}, std::shared_ptr<{{ node.class_name }}>> {{ var(node) }}(m_ast, "{{ node.class_name }}", {{ node.base_class | snake_case }}_);
    {{ var(node) }}.doc() = "{{ node.description }}";
    {% if node.children %}
    {{ var(node) }}.def(py::init<{{ args(node.children) }}>());
    {% endif %}
    {% if node.is_program_node or node.is_ptr_excluded_node %}
    {{ var(node) }}.def(py::init<>());
    {% endif %}

    {{ var(node) }}.def("__repr__", []({{ node.class_name }}& n) {
        std::stringstream ss;
        JSONVisitor v(ss);
        v.compact_json(true);
        n.accept(&v);
        return ss.str();
    });

    {% for member in node.public_members() %}
    {{ var(node) }}.def_readwrite("{{ member[1] }}", &{{ node.class_name }}::{{ member[1] }});
    {% endfor %}

    {{ var(node) }}.def("visit_children", &{{ node.class_name }}::visit_children)
    .def("accept", &{{ node.class_name }}::accept)
    .def("clone", &{{ node.class_name }}::clone)
    .def("get_node_type", &{{ node.class_name }}::get_node_type)
    .def("get_node_type_name", &{{ node.class_name }}::get_node_type_name)
    .def("is_{{ node.class_name | snake_case }}", &{{ node.class_name }}::is_{{ node.class_name | snake_case }});

    {% endfor %}
}

#pragma clang diagnostic pop