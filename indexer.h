#ifndef INDEXER_H
#define INDEXER_H

struct indexer;
struct indexer *indexer_init(const char *self, const char *plugin_dir,
		const char *thumb_dir, const char *conffile);
void indexer_free(struct indexer *indexer);

int indexer_process(struct indexer *indexer, const char *src, const char *dest);
int indexer_rename(struct indexer *indexer, const char *old_path,
		const char *new_path);
int indexer_remove(struct indexer *indexer, const char *path);

#endif
