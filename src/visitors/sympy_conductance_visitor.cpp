/*************************************************************************
 * Copyright (C) 2018-2019 Blue Brain Project
 *
 * This file is part of NMODL distributed under the terms of the GNU
 * Lesser General Public License. See top-level LICENSE file for details.
 *************************************************************************/

#include <algorithm>
#include <iostream>

#include "symtab/symbol.hpp"
#include "utils/logger.hpp"
#include "visitors/lookup_visitor.hpp"
#include "visitors/sympy_conductance_visitor.hpp"
#include "visitors/visitor_utils.hpp"


namespace py = pybind11;
using namespace py::literals;

namespace nmodl {
namespace visitor {

using ast::AstNodeType;
using ast::BinaryOp;
using symtab::syminfo::NmodlType;

/**
 * Analyse breakpoint block to check if it is safe to insert CONDUCTANCE statements
 *
 * Most of the mod files have simple breakpoint blocks without any control flow
 * statements. SympyConductanceVisitor just collects all the statements in the
 * breakpoint block and insert conductance statements. If there are control flow
 * statements like `IF { a } ELSE { b }` block with conflicting current statements
 * inside IF and ELSE blocks or VERBATIM block then the resulting CONDUCTANCE
 * statements may be incorrect. For now the simple approach is to not generate
 * CONDUCTANCE statements if if-else statements exist in the block.
 *
 * @param node Ast node for breakpoint block
 * @return true if it is safe to insert conductance statements otherwise false
 */
static bool conductance_statement_possible(ast::BreakpointBlock* node) {
    AstLookupVisitor v({AstNodeType::IF_STATEMENT, AstNodeType::VERBATIM});
    node->accept(&v);
    return v.get_nodes().empty();
}


// Generate statement strings to be added to BREAKPOINT section
std::vector<std::string> SympyConductanceVisitor::generate_statement_strings(
    ast::BreakpointBlock* node) {
    std::vector<std::string> statements;

    // instead of passing all variables in the symbol table find out variables
    // that are used in the current block and then pass to sympy
    // name could be parameter or unit so check if it exist in symbol table
    auto names_in_block = AstLookupVisitor().lookup(node, AstNodeType::NAME);
    string_set used_names_in_block;
    for (auto& name: names_in_block) {
        auto varname = name->get_node_name();
        if (all_vars.find(varname) != all_vars.end()) {
            used_names_in_block.insert(varname);
        }
    }

    // iterate over binary expression lhs's from breakpoint
    for (const auto& lhs_str: ordered_binary_exprs_lhs) {
        // look for a current name that matches lhs of expr (current write name)
        auto it = i_name.find(lhs_str);
        if (it != i_name.end()) {
            const auto& equation_string = ordered_binary_exprs[binary_expr_index[lhs_str]];
            std::string i_name_str = it->second;
            // SymPy needs the current expression & all previous expressions
            std::vector<std::string> expressions(ordered_binary_exprs.begin(),
                                                 ordered_binary_exprs.begin() +
                                                     binary_expr_index[lhs_str] + 1);
            // differentiate dI/dV
            auto locals = py::dict("expressions"_a = expressions, "vars"_a = used_names_in_block);
            py::exec(R"(
                            from nmodl.ode import differentiate2c
                            exception_message = ""
                            try:
                                rhs = expressions[-1].split("=", 1)[1]
                                solution = differentiate2c(rhs,
                                                           "v",
                                                           vars,
                                                           expressions[:-1]
                                           )
                            except Exception as e:
                                # if we fail, fail silently and return empty string
                                solution = ""
                                exception_message = str(e)
                        )",
                     py::globals(),
                     locals);
            auto dIdV = locals["solution"].cast<std::string>();
            auto exception_message = locals["exception_message"].cast<std::string>();
            if (!exception_message.empty()) {
                logger->warn("SympyConductance :: python exception: {}", exception_message);
            }
            if (dIdV.empty()) {
                logger->warn(
                    "SympyConductance :: analytic differentiation of ionic current "
                    "not possible");
            } else {
                std::string g_var = dIdV;
                // if conductance g_var is not an existing variable, need to generate
                // a new variable name, declare it, and assign it
                if (all_vars.find(g_var) == all_vars.end()) {
                    // generate new variable name
                    std::map<std::string, int> var_map;
                    for (auto const& v: all_vars) {
                        var_map[v] = 0;
                    }
                    g_var = get_new_name("g", i_name_str, var_map);
                    // declare it
                    add_local_variable(node->get_statement_block().get(), g_var);
                    // asign dIdV to it
                    std::string statement_str = g_var;
                    statement_str.append(" = ").append(dIdV);
                    statements.insert(statements.begin(), statement_str);
                    logger->debug("SympyConductance :: Adding BREAKPOINT statement: {}",
                                  statement_str);
                }
                std::string statement_str = "CONDUCTANCE " + g_var;
                if (!i_name_str.empty()) {
                    statement_str += " USEION " + i_name_str;
                }
                statements.push_back(statement_str);
                logger->debug("SympyConductance :: Adding BREAKPOINT statement: {}", statement_str);
            }
        }
    }
    return statements;
}

void SympyConductanceVisitor::visit_binary_expression(ast::BinaryExpression* node) {
    // only want binary expressions from breakpoint block
    if (!under_breakpoint_block) {
        return;
    }
    // only want binary expressions of form x = ...
    if (node->lhs->is_var_name() && (node->op.get_value() == BinaryOp::BOP_ASSIGN)) {
        auto lhs_str =
            std::dynamic_pointer_cast<ast::VarName>(node->lhs)->get_name()->get_node_name();
        binary_expr_index[lhs_str] = ordered_binary_exprs.size();
        ordered_binary_exprs.push_back(to_nmodl_for_sympy(node));
        ordered_binary_exprs_lhs.push_back(lhs_str);
    }
}

void SympyConductanceVisitor::lookup_nonspecific_statements() {
    // add NONSPECIFIC_CURRENT statements to i_name map between write vars and names
    // note that they don't have an ion name, so we set it to ""
    if (!NONSPECIFIC_CONDUCTANCE_ALREADY_EXISTS) {
        for (const auto& ns_curr_ast: nonspecific_nodes) {
            logger->debug("SympyConductance :: Found NONSPECIFIC_CURRENT statement");
            for (const auto& write_name:
                 std::dynamic_pointer_cast<ast::Nonspecific>(ns_curr_ast).get()->get_currents()) {
                std::string curr_write = write_name->get_node_name();
                logger->debug("SympyConductance :: -> Adding non-specific current write name: {}",
                              curr_write);
                i_name[curr_write] = "";
            }
        }
    }
}

void SympyConductanceVisitor::lookup_useion_statements() {
    // add USEION statements to i_name map between write vars and names
    for (const auto& useion_ast: use_ion_nodes) {
        auto ion = std::dynamic_pointer_cast<ast::Useion>(useion_ast).get();
        std::string ion_name = ion->get_node_name();
        logger->debug("SympyConductance :: Found USEION statement {}", to_nmodl_for_sympy(ion));
        if (i_ignore.find(ion_name) != i_ignore.end()) {
            logger->debug("SympyConductance :: -> Ignoring ion current name: {}", ion_name);
        } else {
            for (const auto& w: ion->get_writelist()) {
                std::string ion_write = w->get_node_name();
                logger->debug(
                    "SympyConductance :: -> Adding ion write name: {} for ion current name: {}",
                    ion_write,
                    ion_name);
                i_name[ion_write] = ion_name;
            }
        }
    }
}

void SympyConductanceVisitor::visit_conductance_hint(ast::ConductanceHint* node) {
    // find existing CONDUCTANCE statements - do not want to alter them
    // so keep a set of ion names i_ignore that we should ignore later
    logger->debug("SympyConductance :: Found existing CONDUCTANCE statement: {}",
                  to_nmodl_for_sympy(node));
    if (auto ion = node->get_ion()) {
        logger->debug("SympyConductance :: -> Ignoring ion current name: {}", ion->get_node_name());
        i_ignore.insert(ion->get_node_name());
    } else {
        logger->debug("SympyConductance :: -> Ignoring all non-specific currents");
        NONSPECIFIC_CONDUCTANCE_ALREADY_EXISTS = true;
    }
};

void SympyConductanceVisitor::visit_breakpoint_block(ast::BreakpointBlock* node) {
    // return if it's not safe to insert conductance statements
    if (!conductance_statement_possible(node)) {
        logger->warn("SympyConductance :: Unsafe to insert CONDUCTANCE statement");
        return;
    }

    // add any breakpoint local variables to vars
    if (auto* symtab = node->get_statement_block()->get_symbol_table()) {
        for (const auto& localvar: symtab->get_variables_with_properties(NmodlType::local_var)) {
            all_vars.insert(localvar->get_name());
        }
    }
    // visit BREAKPOINT block statements
    under_breakpoint_block = true;
    node->visit_children(this);
    under_breakpoint_block = false;

    // lookup USEION and NONSPECIFIC statements from NEURON block
    lookup_useion_statements();
    lookup_nonspecific_statements();

    // add new CONDUCTANCE statements to BREAKPOINT
    auto new_statements = generate_statement_strings(node);
    if (!new_statements.empty()) {
        // get a copy of existing BREAKPOINT statements
        auto brkpnt_statements = node->get_statement_block()->get_statements();
        // insert new CONDUCTANCE statements at top of BREAKPOINT
        // or just below LOCAL statement if it exists
        auto insertion_point = brkpnt_statements.begin();
        while ((*insertion_point)->is_local_list_statement()) {
            ++insertion_point;
        }
        for (const auto& statement_str: new_statements) {
            insertion_point = brkpnt_statements.insert(insertion_point,
                                                       create_statement(statement_str));
        }
        // replace old set of BREAKPOINT statements in AST with new one
        node->get_statement_block()->set_statements(std::move(brkpnt_statements));
    }
}

void SympyConductanceVisitor::visit_program(ast::Program* node) {
    all_vars = get_global_vars(node);
    AstLookupVisitor ast_lookup_visitor;
    use_ion_nodes = ast_lookup_visitor.lookup(node, AstNodeType::USEION);
    nonspecific_nodes = ast_lookup_visitor.lookup(node, AstNodeType::NONSPECIFIC);

    node->visit_children(this);
}

}  // namespace visitor
}  // namespace nmodl
