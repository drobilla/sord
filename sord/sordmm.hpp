/* Sord, a lightweight RDF model library.
 * Copyright 2010-2011 David Robillard <d@drobilla.net>
 *
 * Sord is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Sord is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file sordmm.hpp
 * Public Sord C++ API.
 */

#ifndef SORD_SORDMM_HPP
#define SORD_SORDMM_HPP

#include <cassert>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

#include <boost/utility.hpp>
#include <glibmm/ustring.h>

#include "serd/serd.h"
#include "sord/sord.h"

#define SORD_NS_XSD "http://www.w3.org/2001/XMLSchema#"

namespace Sord {

/** C++ wrapper for a Sord object. */
template <typename T>
class Wrapper {
public:
	inline Wrapper(T c_obj = NULL) : _c_obj(c_obj) {}

	inline T       c_obj()       { return _c_obj; }
	inline const T c_obj() const { return _c_obj; }

protected:
	T _c_obj;
};

/** Collection of RDF namespaces with prefixes. */
class Namespaces : public Wrapper<SerdEnv> {
public:
	Namespaces() : Wrapper<SerdEnv>(serd_env_new()) {}

	static inline SerdNode string_to_node(SerdType type, const std::string& s) {
		SerdNode ret = {
			type, s.length() + 1, s.length(), (const uint8_t*)s.c_str() };
		return ret;
	}

	inline void add(const std::string& name,
	                const std::string& uri) {
		const SerdNode name_node = string_to_node(SERD_LITERAL, name);
		const SerdNode uri_node  = string_to_node(SERD_URI,     uri);
		serd_env_add(_c_obj, &name_node, &uri_node);
	}

	inline std::string qualify(std::string uri) const {
		const SerdNode uri_node = string_to_node(SERD_URI, uri);
		SerdNode       prefix;
		SerdChunk      suffix;
		if (serd_env_qualify(_c_obj, &uri_node, &prefix, &suffix)) {
			std::string ret((const char*)prefix.buf, prefix.n_bytes - 1);
			ret.append(":").append((const char*)suffix.buf, suffix.len);
			return ret;
		}
		return uri;
	}

	inline std::string expand(const std::string& curie) const {
		assert(curie.find(":") != std::string::npos);
		SerdNode  curie_node = string_to_node(SERD_CURIE, curie);
		SerdChunk uri_prefix;
		SerdChunk uri_suffix;
		if (serd_env_expand(_c_obj, &curie_node, &uri_prefix, &uri_suffix)) {
			std::string ret((const char*)uri_prefix.buf, uri_prefix.len);
			ret.append((const char*)uri_suffix.buf, uri_suffix.len);
			return ret;
		}
		std::cerr << "CURIE `" << curie << "' has unknown prefix." << std::endl;
		return curie;
	}
};

/** Sord library state. */
class World : public boost::noncopyable, public Wrapper<SordWorld> {
public:
	inline World()
		: _next_blank_id(0)
	{
		_c_obj = sord_world_new();
		add_prefix("rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#");
	}

	inline ~World() {
		sord_world_free(_c_obj);
	}
	
	inline uint64_t blank_id() { return _next_blank_id++; }

	inline void add_prefix(const std::string& prefix, const std::string& uri) {
		_prefixes.add(prefix, uri);
	}

	inline const Namespaces& prefixes() const { return _prefixes; }
	inline SordWorld         world()          { return _c_obj; }

private:
	Namespaces            _prefixes;
	std::set<std::string> _blank_ids;
	uint64_t              _next_blank_id;
};

/** An RDF Node (resource, literal, etc)
 */
class Node : public Wrapper<SordNode> {
public:
	enum Type {
		UNKNOWN  = 0,
		URI      = SORD_URI,
		BLANK    = SORD_BLANK,
		LITERAL  = SORD_LITERAL
	};

	inline Node() : Wrapper<SordNode>(NULL), _world(NULL) {}

	inline Node(World& world, Type t, const std::string& s);
	inline Node(World& world);
	inline Node(World& world, SordNode node);
	inline Node(const Node& other);
	inline ~Node();

	inline Type type() const {
		return _c_obj ? (Type)sord_node_get_type(_c_obj) : UNKNOWN;
	}

	inline SordNode get_node() const { return _c_obj; }

	inline bool is_valid() const { return type() != UNKNOWN; }

	inline bool operator<(const Node& other) const {
		if (type() != other.type()) {
			return type() < other.type();
		} else {
			return to_string() < other.to_string();
		}
	}

	const Node& operator=(const Node& other) {
		if (_c_obj)
			sord_node_free(_c_obj);
		_world = other._world;
		_c_obj = other._c_obj ? sord_node_copy(other._c_obj) : NULL;
		return *this;
	}

	inline bool operator==(const Node& other) const {
		return sord_node_equals(_c_obj, other._c_obj);
	}

	inline const uint8_t* to_u_string() const;
	inline const char*    to_c_string() const;
	inline std::string    to_string() const;

	inline bool is_literal_type(const char* type_uri) const;

	inline bool is_uri()   const { return _c_obj && type() == URI; }
	inline bool is_blank() const { return _c_obj && type() == BLANK; }
	inline bool is_int()   const { return is_literal_type(SORD_NS_XSD "integer"); }
	inline bool is_float() const { return is_literal_type(SORD_NS_XSD "decimal"); }
	inline bool is_bool()  const { return is_literal_type(SORD_NS_XSD "boolean"); }

	inline int   to_int()   const;
	inline float to_float() const;
	inline bool  to_bool()  const;

	inline static Node blank_id(World& world, const std::string base="b") {
		const uint64_t num = world.blank_id();
		std::ostringstream ss;
		ss << base << num;
		return Node(world, Node::BLANK, ss.str());
	}

private:
	World* _world;
};

inline std::ostream&
operator<<(std::ostream& os, const Node& node)
{
	return os << node.to_string() << std::endl;
}

class URI : public Node {
public:
	inline URI(World& world, const std::string& s) : Node(world, Node::URI, s) {}
};

class Curie : public Node {
public:
	inline Curie(World& world, const std::string& s)
		: Node(world, Node::URI, world.prefixes().expand(s)) {}
};

class Literal : public Node {
public:
	inline Literal(World& world, const std::string& s) : Node(world, Node::LITERAL, s) {}
};

inline
Node::Node(World& world, Type type, const std::string& s)
	: _world(&world)
{
	switch (type) {
	case URI:
		assert(s.find(":") == std::string::npos
		       || s.substr(0, 5) == "http:"
		       || s.substr(0, 5) == "file:"
		       || s.substr(0, 4) == "urn:");
		_c_obj = sord_new_uri(
			world.world(), (const unsigned char*)s.c_str());
		break;
	case LITERAL:
		_c_obj = sord_new_literal(
			world.world(), NULL, (const unsigned char*)s.c_str(), NULL);
		break;
	case BLANK:
		_c_obj = sord_new_blank(
			world.world(), (const unsigned char*)s.c_str());
		break;
	default:
		_c_obj = NULL;
	}

	assert(this->type() == type);
}

inline
Node::Node(World& world)
	: _world(&world)
{
	Node me = blank_id(world);
	*this = me;
}

inline
Node::Node(World& world, SordNode node)
	: _world(&world)
{
	_c_obj = node ? sord_node_copy(node) : NULL;
}

inline
Node::Node(const Node& other)
	: Wrapper<SordNode>()
	, _world(other._world)
{
	if (_world) {
		_c_obj = other._c_obj ? sord_node_copy(other._c_obj) : NULL;
	}

	assert((!_c_obj && !other._c_obj) || to_string() == other.to_string());
}

inline
Node::~Node()
{
	if (_world) {
		sord_node_free(_c_obj);
	}
}


inline std::string
Node::to_string() const
{
	return std::string(to_c_string());
}


inline const char*
Node::to_c_string() const
{
	return (const char*)sord_node_get_string(_c_obj);
}

inline const uint8_t*
Node::to_u_string() const
{
	return sord_node_get_string(_c_obj);
}

inline bool
Node::is_literal_type(const char* type_uri) const
{
	if (_c_obj && sord_node_get_type(_c_obj) == SORD_LITERAL) {
		SordNode datatype = sord_node_get_datatype(_c_obj);
		if (datatype && !strcmp((const char*)sord_node_get_string(datatype),
		                        type_uri))
			return true;
	}
	return false;
}

inline int
Node::to_int() const
{
	assert(is_int());
	std::locale c_locale("C");
	std::stringstream ss((const char*)sord_node_get_string(_c_obj));
	ss.imbue(c_locale);
	int i = 0;
	ss >> i;
	return i;
}

inline float
Node::to_float() const
{
	assert(is_float());
	std::locale c_locale("C");
	std::stringstream ss((const char*)sord_node_get_string(_c_obj));
	ss.imbue(c_locale);
	float f = 0.0f;
	ss >> f;
	return f;
}

inline bool
Node::to_bool() const
{
	assert(is_bool());
	return !strcmp((const char*)sord_node_get_string(_c_obj), "true");
}

struct Iter : public Wrapper<SordIter> {
	inline Iter(World& world, SordIter c_obj)
		: Wrapper<SordIter>(c_obj), _world(world) {}
	inline ~Iter() { sord_iter_free(_c_obj); }
	inline bool end()  const { return sord_iter_end(_c_obj); }
	inline bool next() const { return sord_iter_next(_c_obj); }
	inline Iter& operator++() { assert(!end()); next(); return *this; }
	inline Node get_subject() const {
		SordQuad quad;
		sord_iter_get(_c_obj, quad);
		return Node(_world, quad[SORD_SUBJECT]);
	}
	inline Node get_predicate() const {
		SordQuad quad;
		sord_iter_get(_c_obj, quad);
		return Node(_world, quad[SORD_PREDICATE]);
	}
	inline Node get_object() const {
		SordQuad quad;
		sord_iter_get(_c_obj, quad);
		return Node(_world, quad[SORD_OBJECT]);
	}
	World& _world;
};


/** An RDF Model (collection of triples).
 */
class Model : public boost::noncopyable, public Wrapper<SordModel> {
public:
	inline Model(World& world, const Glib::ustring& base_uri=".");
	inline ~Model();

	inline const Node& base_uri() const { return _base; }

	inline void load_file(const Glib::ustring& uri);

	inline void load_string(const char*          str,
	                        size_t               len,
	                        const Glib::ustring& base_uri,
	                        const std::string    lang = "turtle");

	inline void  write_to_file_handle(FILE* fd, const char* lang);
	inline void  write_to_file(const Glib::ustring& uri, const char* lang);
	inline char* write_to_string(const char* lang);

	inline void add_statement(const Node& subject,
	                          const Node& predicate,
	                          const Node& object);

	inline Iter find(const Node& subject,
	                 const Node& predicate,
	                 const Node& object);

	inline World& world() const { return _world; }

private:
	World&     _world;
	Node       _base;
	SerdWriter _writer;
	size_t     _next_blank_id;
};

/** Create an empty in-memory RDF model.
 */
inline
Model::Model(World& world, const Glib::ustring& base_uri)
	: _world(world)
	, _base(world, Node::URI, base_uri)
	, _writer(NULL)
{
	// FIXME: parameters
	_c_obj = sord_new(_world.world(), SORD_SPO|SORD_OPS, true);
}

inline void
Model::load_string(const char*          str,
                   size_t               len,
                   const Glib::ustring& base_uri,
                   const std::string    lang)
{
	// TODO
}

inline Model::~Model()
{
	sord_free(_c_obj);
}

inline void
Model::load_file(const Glib::ustring& data_uri)
{
	// FIXME: blank prefix
	sord_read_file(_c_obj, (const uint8_t*)data_uri.c_str(), NULL,
	               (const uint8_t*)"b");
}

inline void
Model::write_to_file_handle(FILE* fd, const char* lang)
{
	sord_write_file_handle(_c_obj,
	                       _world.prefixes().c_obj(),
	                       fd,
	                       _base.to_u_string(),
	                       NULL,
	                       NULL);
}

inline void
Model::write_to_file(const Glib::ustring& uri, const char* lang)
{
	sord_write_file(_c_obj,
	                _world.prefixes().c_obj(),
	                (const uint8_t*)uri.c_str(),
	                NULL,
	                NULL);
}

inline char*
Model::write_to_string(const char* lang)
{
	std::cerr << "TODO: serialise" << std::endl;
	return NULL;
}

inline void
Model::add_statement(const Node& subject,
                     const Node& predicate,
                     const Node& object)
{
	SordQuad quad = { subject.c_obj(),
	                  predicate.c_obj(),
	                  object.c_obj(),
	                  NULL };

	sord_add(_c_obj, quad);
}

inline Iter
Model::find(const Node& subject,
            const Node& predicate,
            const Node& object)
{
	SordQuad quad = { subject.c_obj(),
	                  predicate.c_obj(),
	                  object.c_obj(),
	                  NULL };

	return Iter(_world, sord_find(_c_obj, quad));
}

} // namespace Sord

#endif // SORD_SORDMM_HPP

