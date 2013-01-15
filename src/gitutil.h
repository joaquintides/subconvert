/*
 * Copyright (c) 2011, BoostPro Computing.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the
 *   distribution.
 *
 * - Neither the name of BoostPro Computing nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _GITUTIL_H
#define _GITUTIL_H

#include "system.hpp"

using namespace boost;

namespace Git
{
  inline void git_check(int result) {
    if (result != 0)
      throw std::logic_error(giterr_last()->message);
  }

  inline std::string git_sha1(const git_oid * git_oid) {
    char checksum[41];
    git_oid_fmt(checksum, git_oid);
    checksum[40] = '\0';
    return checksum;
  }

  class Object;
  class Blob;
  class Tree;
  class Commit;
  class Repository;
  class Branch;

  typedef Repository * RepositoryPtr;

  typedef intrusive_ptr<Object>  ObjectPtr;

  class Object : public noncopyable
  {
    friend class Repository;
    friend class Tree;
    friend class Commit;

  protected:
    RepositoryPtr repository;
    git_oid       oid;

    mutable int refc;

    void acquire() const {
      assert(refc >= 0);
      refc++;
    }
    void release() const {
      assert(refc > 0);
      if (--refc == 0)
        checked_delete(this);
    }

  public:
    std::string  name;
    unsigned int attributes;
    bool         written;

    Object(RepositoryPtr _repository, const git_oid * _oid,
           const std::string& _name = "", unsigned int _attributes = 0)
      : repository(_repository), refc(0), name(_name),
        attributes(_attributes), written(_oid != nullptr) {
      if (_oid != nullptr)
        oid = *_oid;
    }

    virtual ~Object() {
      assert(refc == 0);
    }

    virtual operator const git_oid *() const {
      return &oid;
    }
    virtual const git_oid * get_oid() const {
      return &oid;
    }

    std::string sha1() const {
      return git_sha1(*this);
    }

    virtual bool is_blob() const {
      return true;
    }
    virtual bool is_tree() const {
      return false;
    }

    virtual bool is_modified() const {
      return false;
    }
    virtual bool is_written() const {
      return written;
    }

    virtual ObjectPtr copy_to_name(const std::string& to_name,
                                   bool always_copy = false) = 0;

    virtual void write() {}

    friend inline void intrusive_ptr_add_ref(Object * obj) {
      obj->acquire();
    }
    friend inline void intrusive_ptr_release(Object * obj) {
      obj->release();
    }

#if defined(HAVE_BOOST_SERIALIZATION)
  private:
    /** Serialization. */

    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int /* version */) {
      ar & repository;
      ar & oid;
      ar & refc;
      ar & name;
      ar & attributes;
      ar & written;
    }
#endif // HAVE_BOOST_SERIALIZATION
  };

  typedef intrusive_ptr<Blob> BlobPtr;

  class Blob : public Object
  {
  public:
    Blob(RepositoryPtr repository, const git_oid * _oid,
         const std::string& name, unsigned int attributes = 0100644)
      : Object(repository, _oid, name, attributes) {}

    virtual ObjectPtr copy_to_name(const std::string& to_name,
                                   bool always_copy = false) {
      if (name == to_name && ! always_copy)
        return this;
      else
        return new Blob(repository, &oid, to_name, attributes);
    }

#if defined(HAVE_BOOST_SERIALIZATION)
  private:
    /** Serialization. */

    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int /* version */) {
      ar & boost::serialization::base_object<Object>(*this);
    }
#endif // HAVE_BOOST_SERIALIZATION
  };

  typedef intrusive_ptr<Tree> TreePtr;

  class Tree : public Object
  {
    friend class Repository;

    git_treebuilder * builder;

    friend bool check_size(const Repository& repository, const Tree& tree);

  protected:
    typedef std::map<std::string, ObjectPtr>  entries_map;
    typedef std::pair<std::string, ObjectPtr> entries_pair;

    entries_map entries;
    bool        modified;

    ObjectPtr do_lookup(filesystem::path::iterator segment,
                        filesystem::path::iterator end)
    {
      std::string name = (*segment).string();

      entries_map::iterator i = entries.find(name);
      if (i == entries.end())
        return nullptr;

      ObjectPtr result;
      if (++segment == end) {
        result = (*i).second;
        assert(result->name == name);
      } else {
        result = dynamic_cast<Tree *>((*i).second.get())->do_lookup(segment, end);
      }
      return result;
    }

    bool do_update(filesystem::path::iterator segment,
                   filesystem::path::iterator end, ObjectPtr obj);

    void do_remove(filesystem::path::iterator segment,
                   filesystem::path::iterator end);

  public:
    Tree(RepositoryPtr repository, const git_oid * _oid,
         const std::string& name, unsigned int attributes = 0040000)
      : Object(repository, _oid, name, attributes), builder(nullptr),
        modified(false) {}

    Tree(const Tree& other)
      : Object(other.repository, nullptr, other.name, other.attributes),
        builder(nullptr), entries(other.entries), modified(false) {}

    virtual ~Tree() {
      if (builder != nullptr)
        git_treebuilder_free(builder);
    }

    virtual bool is_blob() const {
      return false;
    }
    virtual bool is_tree() const {
      return true;
    }

    virtual bool is_modified() const {
      return modified;
    }
    virtual bool is_written() const {
      return Object::is_written() && ! is_modified();
    }

    virtual TreePtr copy() {
      return new Tree(*this);
    }
    virtual ObjectPtr copy_to_name(const std::string& to_name, bool = false) {
      TreePtr new_tree(copy());
      new_tree->name = to_name;
      return new_tree;
    }

    bool empty() const {
      return entries.empty();
    }

    ObjectPtr lookup(const filesystem::path& pathname) {
      return do_lookup(pathname.begin(), pathname.end());
    }

    void update(const filesystem::path& pathname, ObjectPtr obj) {
      if (pathname.empty()) {
        assert(obj->is_tree());
        TreePtr subtree = dynamic_cast<Tree *>(obj.get());
        for (auto entry : subtree->entries)
          update(entry.first, entry.second);
      } else {
        do_update(pathname.begin(), pathname.end(), obj);
      }
    }

    void remove(const filesystem::path& pathname) {
      if (pathname.empty()) {
        entries.clear();
        modified = true;
        written = false;
      } else {
        do_remove(pathname.begin(), pathname.end());
      }
    }

    virtual void write();

    void dump_tree(std::ostream& out, int depth = 0);

#if defined(HAVE_BOOST_SERIALIZATION)
  private:
    /** Serialization. */

    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int /* version */) {
      ar & boost::serialization::base_object<Object>(*this);
      ar & builder;
      ar & entries;
      ar & modified;
    }
#endif // HAVE_BOOST_SERIALIZATION
  };

  typedef intrusive_ptr<Commit> CommitPtr;
  typedef intrusive_ptr<Branch> BranchPtr;

  class Commit : public Object
  {
    friend class Repository;

  public:
    CommitPtr   parent;
    TreePtr     tree;
    BranchPtr   branch;
    bool        new_branch;
    std::string message_str;

    shared_ptr<git_signature> signature;

    Commit(RepositoryPtr repo, const git_oid * _oid, CommitPtr _parent = nullptr,
           const std::string& name = "", unsigned int attributes = 0040000)
      : Object(repo, _oid, name, attributes), parent(_parent),
        new_branch(false) {}

    virtual bool is_blob() const {
      return false;
    }
    virtual bool is_tree() const {
      return false;
    }

    virtual bool is_modified() const {
      return tree && tree->is_modified();
    }
    bool is_new_branch() const {
      return new_branch;
    }

    bool has_tree() const;
    void set_tree(TreePtr _tree) {
      tree = _tree;
    }

    virtual ObjectPtr copy_to_name(const std::string& to_name, bool = false) {
      CommitPtr new_commit(clone(true));
      new_commit->name = to_name;
      return new_commit;
    }

    CommitPtr clone(bool with_copy = true);

    std::string get_message() const {
      return message_str;
    }

    void set_message(const std::string& message) {
      message_str = message;
    }

    void set_author(const std::string& name, const std::string& email,
                    time_t time) {
      assert(! name.empty());
      assert(! email.empty());

      git_signature * sig;
      git_check(git_signature_new(&sig, name.c_str(), email.c_str(), time, 0));

      signature = shared_ptr<git_signature>(sig, git_signature_free);
    }

    ObjectPtr lookup(const filesystem::path& pathname) {
      return tree ? tree->lookup(pathname) : tree;
    }

    void update(const filesystem::path& pathname, ObjectPtr obj);
    void remove(const filesystem::path& pathname);

    virtual void write();

    void dump_tree(std::ostream& out) {
      if (tree)
        tree->dump_tree(out);
    }

#if defined(HAVE_BOOST_SERIALIZATION)
  private:
    /** Serialization. */

    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int /* version */) {
      ar & boost::serialization::base_object<Object>(*this);
      ar & parent;
      ar & tree;
      ar & branch;
      ar & new_branch;
      ar & message_str;
      ar & signature;
    }
#endif // HAVE_BOOST_SERIALIZATION
  };

  class Branch : public noncopyable
  {
    friend class Repository;

    git_reference * git_ref;

  public:
    RepositoryPtr    repository;
    std::string      name;
    filesystem::path prefix;
    bool             is_tag;
    CommitPtr        commit;
    CommitPtr        next_commit;

    Branch(RepositoryPtr repo, const std::string& _name = "master",
           bool _is_tag = false)
      : git_ref(nullptr), repository(repo), name(_name), is_tag(_is_tag),
        refc(0) {}

    ~Branch() {
      assert(refc == 0);
      if (git_ref != nullptr)
        git_reference_free(git_ref);
    }

    mutable int refc;

    void acquire() const {
      assert(refc >= 0);
      refc++;
    }
    void release() const {
      assert(refc > 0);
      if (--refc == 0)
        checked_delete(this);
    }

    CommitPtr get_commit(BranchPtr from_branch = nullptr);
    void      update(CommitPtr ptr = nullptr, std::string refname = "");

    friend inline void intrusive_ptr_add_ref(Branch * obj) {
      obj->acquire();
    }
    friend inline void intrusive_ptr_release(Branch * obj) {
      obj->release();
    }

#if defined(HAVE_BOOST_SERIALIZATION)
  private:
    /** Serialization. */

    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int /* version */) {
      ar & repository;
      ar & name;
      ar & prefix;
      ar & is_tag;
      ar & commit;
      ar & next_commit;
    }
#endif // HAVE_BOOST_SERIALIZATION
  };

  struct Logger
  {
    virtual bool debug_mode() const = 0;

    virtual void debug(const std::string& message) const = 0;
    virtual void info(const std::string& message) const = 0;
    virtual void warn(const std::string& message) const = 0;
    virtual void error(const std::string& message) const = 0;

    virtual void newline() const = 0;

    virtual ~Logger() throw() {}
  };

  struct DumbLogger : public Logger
  {
    virtual bool debug_mode() const { return false; }

    virtual void debug(const std::string&) const {}
    virtual void info(const std::string&) const {}
    virtual void warn(const std::string& message) const {
      std::cerr << message << std::endl;
    }
    virtual void error(const std::string& message) const {
      std::cerr << message << std::endl;
    }
  };

  inline void no_commit_info(CommitPtr) {}

  class Repository
  {
    git_repository * repo;

  public:
    typedef std::map<std::string, BranchPtr>      branches_name_map;
    typedef branches_name_map::value_type         branches_name_value;

    typedef std::map<filesystem::path, BranchPtr> branches_path_map;
    typedef branches_path_map::value_type         branches_path_value;

    Logger&                   log;
    std::string               repo_name;
    branches_name_map         branches_by_name;
    branches_path_map         branches_by_path;
    std::vector<CommitPtr>    commit_queue;
    function<void(CommitPtr)> set_commit_info;

    Repository(const filesystem::path& pathname, Logger& _log,
               function<void(CommitPtr)> _set_commit_info = no_commit_info)
      : repo(nullptr), log(_log), set_commit_info(_set_commit_info)
    {
      if (git_repository_open(&repo, pathname.string().c_str()) != 0)
        if (git_repository_open(&repo,
                                (pathname / ".git").string().c_str()) != 0)
          throw std::logic_error(std::string("Could not open repository: ") +
                                 pathname.string() + " or " +
                                 (pathname / ".git").string());
    }
    ~Repository() {
      if (repo != nullptr)
        git_repository_free(repo);
    }

    operator git_repository *() const {
      return repo;
    }

    BlobPtr   create_blob(const std::string& name,
                          const char * data, std::size_t len,
                          unsigned int attributes = 0100644);

    TreePtr   create_tree(const std::string& name = "",
                          unsigned int attributes = 040000);

    CommitPtr create_commit(CommitPtr parent = nullptr);

    BranchPtr find_branch_by_name(const std::string& name,
                                  BranchPtr default_obj = nullptr);
    BranchPtr find_branch_by_path(const filesystem::path& name,
                                  BranchPtr default_obj = nullptr);
    void      delete_branch(BranchPtr branch, int related_revision);
    bool      write(int related_revision);
    void      write_branches();
    void      garbage_collect();

    void      create_tag(CommitPtr commit, const std::string& name);
    void      create_file(const filesystem::path& pathname,
                          const std::string& content = "");

    const filesystem::path dotgit_directory() const
    {
        return filesystem::path(::git_repository_path(repo));
    }

#if defined(HAVE_BOOST_SERIALIZATION)
  private:
    /** Serialization. */

    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int /* version */) {
      ar & repo;
      ar & log;
      ar & repo_name;
      ar & branches_by_name;
      ar & branches_by_path;
      ar & commit_queue;
      ar & set_commit_info;
    }
#endif // HAVE_BOOST_SERIALIZATION
  };
}

#endif // _GITUTIL_H
