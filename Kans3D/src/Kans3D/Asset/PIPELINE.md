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
