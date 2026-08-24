title: On Incremental Builds
date: 2026-02-10

# On Incremental Builds

Rebuilding every page on every run wastes time once a site has hundreds of posts.

This generator hashes each source file with SHA-256 and compares it against a
cached hash from the previous build stored in `.buildcache.json`. Only pages whose
hash changed get regenerated.

- Fast rebuilds
- Simple, dependency-free implementation
