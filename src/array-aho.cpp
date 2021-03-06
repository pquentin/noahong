// Copyright (C) 2012, 2014 Jeff Donner
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// (MIT 'expat' license)

// Description of Aho-Corasick:
//   http://www.cs.uku.fi/~kilpelai/BSA05/lectures/slides04.pdf

#include "array-aho.h"
#include <queue>
#include <iostream>
#include <utility>


using namespace std;


std::ostream& operator<<(std::ostream& os, Node const& node) {
   const Node::Children& children = node.get_children();
   for (Node::Children::const_iterator i = children.begin(),
           end = children.end();
        i != end; ++i) {
      os << (char)i->first << ';';
   }
   os << ((0 < node.length) ? " (term) " : "()");
   os << " and failure: " << node.ifailure_state;
   os << std::endl;
   return os;
}


std::ostream& operator<<(std::ostream& os, AhoCorasickTrie::Chars const& text) {
   for (AhoCorasickTrie::Chars::const_iterator i = text.begin(), end = text.end();
        i != end; ++i) {
      os << (char)*i;
   }
   return os;
}


std::ostream& operator<<(std::ostream& os, AhoCorasickTrie::Strings const& texts) {
   for (AhoCorasickTrie::Strings::const_iterator i = texts.begin(), end = texts.end();
        i != end; ++i) {
      os << '[' << *i << ']' << endl;
   }
   return os;
}

typedef std::deque<Node::Child> FrozenChildren;

struct FrozenNode {
   typedef int Index;

   FrozenNode()
   : length(0)
   , ifailure_state(0)
   , payload()
   , children_offset(0)
   , children_count(0)
   {}

   Index child_at(const FrozenChildren& children, AC_CHAR_TYPE c) const {
      const FrozenChildren::const_iterator begin = children.begin() + children_offset;
      const FrozenChildren::const_iterator end = begin + children_count;
      const FrozenChildren::const_iterator child = std::lower_bound(
              begin, end, Node::Child(c, -1));
      if (child == end || child->first != c)
          // since these are indices, 0 is valid, so invalid is < 0
          return -1;
      return child->second;
   }

   unsigned short length;
   Index ifailure_state;
   PayloadT payload;
   int32_t children_offset;
   int32_t children_count;
};


class FrozenTrie {
public:
   typedef Node::Index Index;

   FrozenTrie(Nodes&);

   PayloadT find_short(char const* s, size_t n,
                       int* inout_start,
                       int* out_end) const;

   PayloadT find_longest(char const* s, size_t n,
                         int* inout_start,
                         int* out_end) const;

   int contains(char const*, size_t n) const;

   int num_keys() const;

   int num_nodes() const;

   int num_total_children() const;

   /// Returns either a valid ptr (including 0) or, -1 cast as a ptr.
   PayloadT get_payload(char const* s, size_t n) const;


private:
   Index child_at(Index i, AC_CHAR_TYPE a) const;

   // root is at 0 of course.
   typedef std::deque<FrozenNode> FrozenNodes;
   FrozenNodes nodes;
   FrozenChildren children;
};


FrozenTrie::FrozenTrie(Nodes& source_nodes) {
   while (!source_nodes.empty()) {
      const Node& n = source_nodes.front();
      const Node::Children& n_children = n.get_children();
      FrozenNode f;
      f.length = n.length;
      f.ifailure_state = n.ifailure_state;
      f.payload = n.payload;
      f.children_offset = children.size();
      f.children_count = n_children.size();
      nodes.push_back(f);

      Node::Children::const_iterator it;
      for (it = n_children.begin(); it != n_children.end(); ++it)
          children.push_back(*it);

      source_nodes.pop_front();
   }
}


Node::Index FrozenTrie::child_at(Index i, AC_CHAR_TYPE a) const {
    Index ichild = nodes[i].child_at(children, a);
    // The root is a special case - every char that's not an actual
    // child of the root, points back to the root.
    if (ichild < 0 && i == 0)
        ichild = 0;
    return ichild;
}


PayloadT FrozenTrie::find_short(char const* char_s, size_t n,
                                int* inout_istart,
                                int* out_iend) const {
   Index istate = 0;
   PayloadT last_payload = 0;
   AC_CHAR_TYPE const* original_start = reinterpret_cast<AC_CHAR_TYPE const*>(char_s);
   AC_CHAR_TYPE const* start = original_start + *inout_istart;
   AC_CHAR_TYPE const* end = original_start + n;

   for (AC_CHAR_TYPE const* c = start; c < end; ++c) {
      Index ichild = this->child_at(istate, *c);
      while (ichild < 0) {
         istate = nodes[istate].ifailure_state;
         ichild = this->child_at(istate, *c);
      }

      istate = ichild;
      if (nodes[istate].length and nodes[istate].length <= c + 1 - start) {
         *out_iend = c - original_start + 1;
         *inout_istart = *out_iend - nodes[istate].length;
         return nodes[istate].payload;
      }
   }
   return last_payload;
}


/*
 * <char_s> is the original material,
 * <n> its length.
 * <inout_istart> is offset from char_s, part of the caller's traversal
 *   state, to let zer get multiple matches from the same text.
 * <out_iend> one-past-the-last offset from <char_s> of /this/ match, the
 *   mate to <inout_istart>.
 *
 * Does not itself assure that the match bounds point to nothing (ie end ==
 * start) when nothing is found. The caller must do that (and the Python
 * interface, and the gtests do that.
 *
 * When there are multiple contiguous terminal nodes (keywords that end at some
 * spot) multiple calls of this will be O(n^2) in that contiguous length - it
 * looks through all contiguous matches to find the longest one before returning
 * anything.
 */
PayloadT FrozenTrie::find_longest(char const* char_s, size_t n,
                                  int* inout_istart,
                                  int* out_iend) const {
   // longest terminal length, among a contiguous bunch of terminals.
   int length_longest = -1;
   int end_longest = -1;
   Index istate = 0;
   bool have_match = false;

   PayloadT payload = 0;
   AC_CHAR_TYPE const* original_start =
      reinterpret_cast<AC_CHAR_TYPE const*>(char_s);
   AC_CHAR_TYPE const* start = original_start + *inout_istart;
   AC_CHAR_TYPE const* end = original_start + n;

   for (AC_CHAR_TYPE const* c = start; c < end; ++c) {
      Index ichild = this->child_at(istate, *c);
      while (ichild < 0) {
         if (have_match) {
            goto success;
         }
         istate = nodes[istate].ifailure_state;
         ichild = this->child_at(istate, *c);
      }

      istate = ichild;
      int keylen = nodes[istate].length;
      if (keylen &&
          // not sure this 2nd condition is necessary
          keylen <= c + 1 - start &&
          length_longest < keylen) {
         have_match = true;

         length_longest = keylen;
         end_longest = c + 1 - original_start;
         payload = nodes[istate].payload;
      }
   }
   if (have_match) {
success:
      *out_iend = end_longest;
      *inout_istart = *out_iend - length_longest;
   }
   return payload;
}


int FrozenTrie::contains(char const* char_s, size_t n) const {
   AC_CHAR_TYPE const* c = reinterpret_cast<AC_CHAR_TYPE const*>(char_s);
   Index inode = 0;
   for (size_t i = 0; i < n; ++i, ++c) {
      inode = nodes[inode].child_at(children, *c);
      if (inode < 0) {
         return 0;
      }
   }

   return nodes[inode].length ? 1 : 0;
}


int FrozenTrie::num_keys() const {
   int num = 0;
   for (FrozenNodes::const_iterator it = nodes.begin(), end = nodes.end();
        it != end; ++it) {
      if (it->length)
         ++num;
   }

   return num;
}


int FrozenTrie::num_total_children() const {
   return children.size();
}


int FrozenTrie::num_nodes() const {
   return nodes.size();
}


PayloadT FrozenTrie::get_payload(char const* s, size_t n) const {
   AC_CHAR_TYPE const* ucs4 = (AC_CHAR_TYPE const*)s;
   AC_CHAR_TYPE const* u = ucs4;

   Node::Index inode = 0;
   for (u = ucs4; u < ucs4 + n; ++u) {
      inode = nodes[inode].child_at(children, *u);
      if (inode < 0)
         return (PayloadT)-1;
   }
   if (nodes[inode].length)
      return nodes[inode].payload;
   else
      return (PayloadT)-1;
}


AhoCorasickTrie::AhoCorasickTrie() {
    // born with root node
    nodes.push_back(Node());
}


AhoCorasickTrie::~AhoCorasickTrie() {
}


void AhoCorasickTrie::add_string(char const* char_s, size_t n,
                                 PayloadT payload) {

   if(frozen)
      throw std::runtime_error("cannot add entry to compiled trie");

   AC_CHAR_TYPE const* c = reinterpret_cast<AC_CHAR_TYPE const*>(char_s);

   Index iparent = 0;
   Index ichild = 0;
   for (size_t i = 0; i < n; ++i, ++c) {
      // Don't need the node here, it's behind the scenes.
      // on the other hand we don't care about the speed of adding
      // strings.
      ichild = nodes[iparent].child_at(*c);
      if (not is_valid(ichild)) {
         ichild = add_node();
         nodes[iparent].set_child(*c, ichild);
      }
      iparent = ichild;
   }
   nodes[ichild].payload = payload;
   nodes[ichild].length = n;
}


int AhoCorasickTrie::contains(char const* char_s, size_t n) const {
   assert_compiled();
   return frozen->contains(char_s, n);
}


int AhoCorasickTrie::num_keys() const {
   if (frozen)
       return frozen->num_keys();
   int num = 0;
   for (Nodes::const_iterator it = nodes.begin(), end = nodes.end();
        it != end; ++it) {
      if (it->length)
         ++num;
   }

   return num;
}


int AhoCorasickTrie::num_total_children() const {
   if (frozen)
       return frozen->num_total_children();
   int num = 0;
   for (Nodes::const_iterator it = nodes.begin(), end = nodes.end();
        it != end; ++it) {
      num += it->get_children().size();
   }

   return num;
}


int AhoCorasickTrie::num_nodes() const {
   if (frozen)
       return frozen->num_nodes();
   return nodes.size();
}


void AhoCorasickTrie::compile() {
   if (frozen)
      return;
   make_failure_links();
   frozen.reset(new FrozenTrie(nodes));
}


PayloadT AhoCorasickTrie::get_payload(char const* s, size_t n) const {
   assert_compiled();
   return frozen->get_payload(s, n);
}


// After:
//   http://www.quretec.com/u/vilo/edu/2005-06/Text_Algorithms/index.cgi?f=L2_Multiple_String&p=ACpre
void AhoCorasickTrie::make_failure_links() {
   queue<Node*> q;
   const Node::Children& children = nodes[Index(0)].get_children();
   for (Node::Children::const_iterator i = children.begin(),
           end = children.end();
        i != end; ++i) {
      Node* child = &nodes[i->second];
      q.push(child);
      child->ifailure_state = (Index)0;
   }
   // root fails to root
   nodes[0].ifailure_state = 0;

   while (not q.empty()) {
      Node* r = q.front();
      q.pop();
      const Node::Children& children = r->get_children();
      for (Node::Children::const_iterator is = children.begin(),
              end = children.end();
           is != end; ++is) {
         AC_CHAR_TYPE a = is->first;
         Node* s = &nodes[is->second];
         q.push(s);
         Index ifail_state = r->ifailure_state;
         Index ifail_child = this->child_at(ifail_state, a);
         while (not is_valid(ifail_child)) {
            ifail_state = nodes[ifail_state].ifailure_state;
            ifail_child = this->child_at(ifail_state, a);
         }
         s->ifailure_state = ifail_child;
      }
   }
}


bool AhoCorasickTrie::is_valid(Index ichild) {
   return 0 <= ichild;
}


void AhoCorasickTrie::assert_compiled() const {
   if (!frozen)
       throw std::runtime_error("trie must be compiled before use");
}


Node::Index AhoCorasickTrie::child_at(Index i, AC_CHAR_TYPE a) const {
    Index ichild = nodes[i].child_at(a);
    // The root is a special case - every char that's not an actual
    // child of the root, points back to the root.
    if (ichild < 0 && i == 0)
        ichild = 0;
    return ichild;
}


Node::Index AhoCorasickTrie::add_node() {
    nodes.push_back(Node());
    return nodes.size() - 1;
}


PayloadT AhoCorasickTrie::find_short(char const* char_s, size_t n,
                                     int* inout_istart,
                                     int* out_iend) const {
   assert_compiled();
   return frozen->find_short(char_s, n, inout_istart, out_iend);
}

/*
 * <char_s> is the original material,
 * <n> its length.
 * <inout_istart> is offset from char_s, part of the caller's traversal
 *   state, to let zer get multiple matches from the same text.
 * <out_iend> one-past-the-last offset from <char_s> of /this/ match, the
 *   mate to <inout_istart>.
 *
 * Does not itself assure that the match bounds point to nothing (ie end ==
 * start) when nothing is found. The caller must do that (and the Python
 * interface, and the gtests do that.
 *
 * When there are multiple contiguous terminal nodes (keywords that end at some
 * spot) multiple calls of this will be O(n^2) in that contiguous length - it
 * looks through all contiguous matches to find the longest one before returning
 * anything.
 */
PayloadT AhoCorasickTrie::find_longest(char const* char_s, size_t n,
                                       int* inout_istart,
                                       int* out_iend) const {
   assert_compiled();
   return frozen->find_longest(char_s, n, inout_istart, out_iend);
}


// For debugging.
void AhoCorasickTrie::print() const {
   typedef pair<AC_CHAR_TYPE, Index> Pair;
   queue<Pair> q;
   q.push(make_pair((AC_CHAR_TYPE)'@', 0));
   while (not q.empty()) {
      Pair p = q.front();
      q.pop();
      AC_CHAR_TYPE f = p.first;
      if (f == '$') {
         cout << endl;
         continue;
      } else {
         cout << (char)p.first << " ";
      }
      Index inode = p.second;
      if (is_valid(inode)) {
         const Node::Children& children = nodes[inode].get_children();
         for (Node::Children::const_iterator i = children.begin(),
                 end = children.end();
              i != end; ++i) {
            // structurally the same; will it work?
            q.push(make_pair(i->first, i->second));
         }
         // mark level
         q.push(make_pair<AC_CHAR_TYPE, Index>('$', 0));
      }
   }
}
