# Measurement rejection contracts

Synthetic reports independently verify template ownership, zero hits, source mappings, source/test separation within one owner, integer count validity, Python branch metrics and unmeasured tooling inputs. No external compiler or production report is needed to exercise rejection.

```mermaid
flowchart LR
  Report[Independent raw report] --> Measurement[Owner normalization]
  Measurement --> Gap[Uncovered and unmapped code stays visible]
  Invalid[Unknown or missing configured mapping] --> Reject[Measurement fails]
```

Run `python3 -m unittest scripts.quality.coverage.measurement.tests.test_report`. A live project report passing reconciliation does not replace these negative cases.

Independent LLVM summaries distinguish one fully covered source function from two template instantiations with one uncalled. Missing, negative, over-covered or boolean instantiation counts fail; per-file, owner and total counts must preserve the actual template gap.

Run scope contracts with `python3 -m unittest scripts.quality.coverage.measurement.tests.test_scope`. They select owners across arbitrary paths, exclude an unrelated target even inside a similar folder, retain selected-part gaps, reject unknown sources and malformed selections, and verify C++-only/Python-only CLI inputs. Complete-component selection includes declared code and metadata parts; groupings and unsupported-only requests fail. Shared source/report fixtures contain no test methods, so reuse does not duplicate test discovery.
