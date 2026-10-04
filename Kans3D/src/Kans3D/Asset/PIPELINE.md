# Import and load boundary

Tool pipeline:
`SourceAssetID + settings -> ImporterRegistry -> ModelImporter -> complete staged Product set`.

Runtime pipeline:
`AssetID -> registry/location -> byte IO -> Loader<StaticMesh> -> CPU StaticMesh`.

- StaticMeshImporter is removed, not renamed into a builder.
- Importers receive source context and emit descriptors with stable LocalIDs.
  They do not return runtime assets, commit files, mutate the registry or create GPU resources.
- AssetImporter selects by source extension and validates descriptors. It is only
  the staging phase, not an implemented transaction/commit system.
- Loader consumes borrowed Product bytes, never source files. AssetManager will
  own location resolution, IO and caching; renderer owns GPU upload.
- ModelImporter and Loader<StaticMesh> explicitly return NotImplemented until
  CPU parser extraction, a versioned Mesh Product format and decoding exist.
- ImporterPanel's std::async job/LegacyImportResult, MeshSourceImporter and the editor preview callback
  remain legacy direct-preview code. They are not wired into the new pipeline.
  Texture/Environment legacy stubs are unregistered and use a separate legacy base.
- Next: extract ImportedModelData; implement ModelImporter output and LocalID
  mapping; implement Mesh Product decoding; add atomic product-set publication;
  connect AssetManager and migrate the editor panel. Existing source database,
  .kmeta and Project initialization are unchanged.

Neither the legacy .ksmesh raw source-reference header nor an in-memory
SourceAssetID object is the new Product file format.

## Source mutations and missing records

- SourceAssetDatabase mutations are record-only: UpdateSource keeps ID/path,
  ChangeSourcePath keeps ID/metadata, and UnregisterSource releases both indexes.
  They never move/delete source files, .kmeta sidecars or Products.
- Project scans reconcile by SourceAssetID. Absent sources keep their last path,
  size and timestamp with Exists=false/Missing=true, including across cache reopen.
  A matching sidecar restores the record or moves it to its current scanned path.
- Missing records reserve their last path. A different source ID at that path is
  a conflict; refresh/open preserves the previous database/cache rather than
  silently transferring references or discarding the old identity.
- Scan and reconciliation errors leave the live database/cache unchanged. Sidecars
  created during the scan can persist even when reconciliation fails.
- Missing history depends on the rebuildable cache/current database. Deleting all
  identity/history files cannot recover old records. Live source identity remains
  authoritative in .kmeta.
- Explicit editor asset deletion is a future project-level operation coordinating
  files, sidecars, Products and record unregistration. Product removal/retention
  is not decided by marking a source Missing.
