/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <stdlib.h>
#include <string.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

struct menu rootmenu;
struct menu *current_menu, *current_entry;
static struct menu **last_entry_ptr;

struct file *file_list;
struct file *current_file;

void menu_init(void)
{
	current_entry = current_menu = &rootmenu;
	last_entry_ptr = &rootmenu.list;
}

void menu_add_entry(struct symbol *sym)
{
	struct menu *menu;

	menu = malloc(sizeof(*menu));
	memset(menu, 0, sizeof(*menu));
	menu->sym = sym;
	menu->parent = current_menu;
	menu->file = current_file;
	menu->lineno = zconf_lineno();

	*last_entry_ptr = menu;
	last_entry_ptr = &menu->next;
	current_entry = menu;
}

void menu_end_entry(void)
{
}

void menu_add_menu(void)
{
	current_menu = current_entry;
	last_entry_ptr = &current_entry->list;
}

void menu_end_menu(void)
{
	last_entry_ptr = &current_menu->next;
	current_menu = current_menu->parent;
}

struct expr *menu_check_dep(struct expr *e)
{
	if (!e)
		return e;

	switch (e->type) {
	case E_NOT:
		e->left.expr = menu_check_dep(e->left.expr);
		break;
	case E_OR:
	case E_AND:
		e->left.expr = menu_check_dep(e->left.expr);
		e->right.expr = menu_check_dep(e->right.expr);
		break;
	case E_SYMBOL:
		/* change 'm' into 'm' && MODULES */
		if (e->left.sym == &symbol_mod)
			return expr_alloc_and(e, expr_alloc_symbol(modules_sym));
		break;
	default:
		break;
	}
	return e;
}

void menu_add_dep(struct expr *dep)
{
	current_entry->dep = expr_alloc_and(current_entry->dep, menu_check_dep(dep));
}

void menu_set_type(int type)
{
	struct symbol *sym = current_entry->sym;

	if (sym->type == type)
		return;
	if (sym->type == S_UNKNOWN) {
		sym->type = type;
		return;
	}
	fprintf(stderr, "%s:%d:warning: type of '%s' redefined from '%s' to '%s'\n",
		current_entry->file->name, current_entry->lineno,
		sym->name ? sym->name : "<choice>", sym_type_name(sym->type), sym_type_name(type));
}

struct property *menu_add_prop(enum prop_type type, char *prompt, struct expr *expr, struct expr *dep)
{
	struct property *prop = prop_alloc(type, current_entry->sym);

	prop->menu = current_entry;
	prop->text = prompt;
	prop->expr = expr;
	prop->visible.expr = menu_check_dep(dep);

	if (prompt) {
		if (current_entry->prompt)
			fprintf(stderr, "%s:%d: prompt redefined\n",
				current_entry->file->name, current_entry->lineno);
		current_entry->prompt = prop;
	}

	return prop;
}

void menu_add_prompt(enum prop_type type, char *prompt, struct expr *dep)
{
	menu_add_prop(type, prompt, NULL, dep);
}

void menu_add_expr(enum prop_type type, struct expr *expr, struct expr *dep)
{
	menu_add_prop(type, NULL, expr, dep);
}

void menu_add_symbol(enum prop_type type, struct symbol *sym, struct expr *dep)
{
	menu_add_prop(type, NULL, expr_alloc_symbol(sym), dep);
}

void menu_finalize(struct menu *parent)
{
	struct menu *menu, *last_menu;
	struct symbol *sym;
	struct property *prop;
	struct expr *parentdep, *basedep, *dep, *dep2, **ep;

	sym = parent->sym;
	if (parent->list) {
		if (sym && sym_is_choice(sym)) {
			/* find the first choice value and find out choice type */
			for (menu = parent->list; menu; menu = menu->next) {
				if (menu->sym) {
					current_entry = parent;
					menu_set_type(menu->sym->type);
					current_entry = menu;
					menu_set_type(sym->type);
					break;
				}
			}
			parentdep = expr_alloc_symbol(sym);
		} else if (parent->prompt)
			parentdep = parent->prompt->visible.expr;
		else
			parentdep = parent->dep;

		for (menu = parent->list; menu; menu = menu->next) {
			basedep = expr_transform(menu->dep);
			basedep = expr_alloc_and(expr_copy(parentdep), basedep);
			basedep = expr_eliminate_dups(basedep);
			menu->dep = basedep;
			if (menu->sym)
				prop = menu->sym->prop;
			else
				prop = menu->prompt;
			for (; prop; prop = prop->next) {
				if (prop->menu != menu)
					continue;
				dep = expr_transform(prop->visible.expr);
				dep = expr_alloc_and(expr_copy(basedep), dep);
				dep = expr_eliminate_dups(dep);
				if (menu->sym && menu->sym->type != S_TRISTATE)
					dep = expr_trans_bool(dep);
				prop->visible.expr = dep;
				if (prop->type == P_SELECT) {
					struct symbol *es = prop_get_symbol(prop);
					es->rev_dep.expr = expr_alloc_or(es->rev_dep.expr,
							expr_alloc_and(expr_alloc_symbol(menu->sym), expr_copy(dep)));
				}
			}
		}
		for (menu = parent->list; menu; menu = menu->next)
			menu_finalize(menu);
	} else if (sym) {
		basedep = parent->prompt ? parent->prompt->visible.expr : NULL;
		basedep = expr_trans_compare(basedep, E_UNEQUAL, &symbol_no);
		basedep = expr_eliminate_dups(expr_transform(basedep));
		last_menu = NULL;
		for (menu = parent->next; menu; menu = menu->next) {
			dep = menu->prompt ? menu->prompt->visible.expr : menu->dep;
			if (!expr_contains_symbol(dep, sym))
				break;
			if (expr_depends_symbol(dep, sym))
				goto next;
			dep = expr_trans_compare(dep, E_UNEQUAL, &symbol_no);
			dep = expr_eliminate_dups(expr_transform(dep));
			dep2 = expr_copy(basedep);
			expr_eliminate_eq(&dep, &dep2);
			expr_free(dep);
			if (!expr_is_yes(dep2)) {
				expr_free(dep2);
				break;
			}
			expr_free(dep2);
		next:
			menu_finalize(menu);
			menu->parent = parent;
			last_menu = menu;
		}
		if (last_menu) {
			parent->list = parent->next;
			parent->next = last_menu->next;
			last_menu->next = NULL;
		}
	}
	for (menu = parent->list; menu; menu = menu->next) {
		if (sym && sym_is_choice(sym) && menu->sym) {
			menu->sym->flags |= SYMBOL_CHOICEVAL;
			for (prop = menu->sym->prop; prop; prop = prop->next) {
				if (prop->type != P_DEFAULT)
					continue;
				fprintf(stderr, "%s:%d:warning: defaults for choice values not supported\n",
					prop->file->name, prop->lineno);
			}
			current_entry = menu;
			menu_set_type(sym->type);
			menu_add_symbol(P_CHOICE, sym, NULL);
			prop = sym_get_choice_prop(sym);
			for (ep = &prop->expr; *ep; ep = &(*ep)->left.expr)
				;
			*ep = expr_alloc_one(E_CHOICE, NULL);
			(*ep)->right.sym = menu->sym;
		}
		if (menu->list && (!menu->prompt || !menu->prompt->text)) {
			for (last_menu = menu->list; ; last_menu = last_menu->next) {
				last_menu->parent = parent;
				if (!last_menu->next)
					break;
			}
			last_menu->next = menu->next;
			menu->next = menu->list;
			menu->list = NULL;
		}
	}

	if (sym && !(sym->flags & SYMBOL_WARNED)) {
		struct symbol *sym2;
		if (sym->type == S_UNKNOWN)
			fprintf(stderr, "%s:%d:warning: config symbol defined without type\n",
				parent->file->name, parent->lineno);

		if (sym_is_choice(sym) && !parent->prompt)
			fprintf(stderr, "%s:%d:warning: choice must have a prompt\n",
				parent->file->name, parent->lineno);

		for (prop = sym->prop; prop; prop = prop->next) {
			switch (prop->type) {
			case P_DEFAULT:
				if ((sym->type == S_STRING || sym->type == S_INT || sym->type == S_HEX) &&
				    prop->expr->type != E_SYMBOL)
					fprintf(stderr, "%s:%d:warning: default must be a single symbol\n",
						prop->file->name, prop->lineno);
				break;
			case P_SELECT:
				sym2 = prop_get_symbol(prop);
				if ((sym->type != S_BOOLEAN && sym->type != S_TRISTATE) ||
				    (sym2->type != S_BOOLEAN && sym2->type != S_TRISTATE))
					fprintf(stderr, "%s:%d:warning: enable is only allowed with boolean and tristate symbols\n",
						prop->file->name, prop->lineno);
				break;
			case P_RANGE:
				if (sym->type != S_INT && sym->type != S_HEX)
					fprintf(stderr, "%s:%d:warning: range is only allowed for int or hex symbols\n",
						prop->file->name, prop->lineno);
				if (!sym_string_valid(sym, prop->expr->left.sym->name) ||
				    !sym_string_valid(sym, prop->expr->right.sym->name))
					fprintf(stderr, "%s:%d:warning: range is invalid\n",
						prop->file->name, prop->lineno);
				break;
			default:
				;
			}
		}
		sym->flags |= SYMBOL_WARNED;
	}

	if (sym && !sym_is_optional(sym) && parent->prompt) {
		sym->rev_dep.expr = expr_alloc_or(sym->rev_dep.expr,
				expr_alloc_and(parent->prompt->visible.expr,
					expr_alloc_symbol(&symbol_mod)));
	}
}

bool menu_is_visible(struct menu *menu)
{
	struct menu *child;
	struct symbol *sym;
	tristate visible;

	if (!menu->prompt)
		return false;
	sym = menu->sym;
	if (sym) {
		sym_calc_value(sym);
		visible = menu->prompt->visible.tri;
	} else
		visible = menu->prompt->visible.tri = expr_calc_value(menu->prompt->visible.expr);

	if (sym && sym_is_choice(sym)) {
		for (child = menu->list; child; child = child->next)
			if (menu_is_visible(child))
				break;
		if (!child)
			return false;
	}

	if (visible != no)
		return true;
	if (!sym || sym_get_tristate_value(menu->sym) == no)
		return false;

	for (child = menu->list; child; child = child->next)
		if (menu_is_visible(child))
			return true;
	return false;
}

const char *menu_get_prompt(struct menu *menu)
{
	if (menu->prompt)
		return menu->prompt->text;
	else if (menu->sym)
		return menu->sym->name;
	return NULL;
}

struct menu *menu_get_root_menu(struct menu *menu)
{
	return &rootmenu;
}

struct menu *menu_get_parent_menu(struct menu *menu)
{
	enum prop_type type;

	for (; menu != &rootmenu; menu = menu->parent) {
		type = menu->prompt ? menu->prompt->type : 0;
		if (type == P_MENU)
			break;
	}
	return menu;
}

struct file *file_lookup(const char *name)
{
	struct file *file;

	for (file = file_list; file; file = file->next) {
		if (!strcmp(name, file->name))
			return file;
	}

	file = malloc(sizeof(*file));
	memset(file, 0, sizeof(*file));
	file->name = strdup(name);
	file->next = file_list;
	file_list = file;
	return file;
}

int file_write_dep(const char *name)
{
	struct file *file;
	FILE *out;

	if (!name)
		name = ".config.cmd";
	out = fopen("..config.tmp", "w");
	if (!out)
		return 1;
	fprintf(out, "deps_config := \\\n");
	for (file = file_list; file; file = file->next) {
		if (file->next)
			fprintf(out, "\t%s \\\n", file->name);
		else
			fprintf(out, "\t%s\n", file->name);
	}
	fprintf(out, "\n.config include/linux/autoconf.h: $(deps_config)\n\n$(deps_config):\n");
	fclose(out);
	rename("..config.tmp", name);
	return 0;
}

