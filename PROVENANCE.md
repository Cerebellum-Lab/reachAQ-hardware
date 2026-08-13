# Project provenance and contributors

## Repository lineage

- Maintained repository: [`Cerebellum-Lab/reachAQ-hardware`](https://github.com/Cerebellum-Lab/reachAQ-hardware)
- Original upstream repository: [`Mouse-GYM/auto-trainer-hardware`](https://github.com/Mouse-GYM/auto-trainer-hardware)
- Upstream revision at the start of Cerebellum Lab development:
  `86cb577f9bb163fc7e5ae553520b2d7c81535c11`
- Upstream project name: Autotrainer / MouseGym

The maintained repository is a GitHub fork, not a source-only copy. Its commit
history, author metadata, tags, and GitHub fork relationship preserve the
original development record. New Cerebellum Lab work should be pushed only to
the maintained repository. The upstream remote is retained for attribution and
for explicitly requested upstream synchronization.

## Commit authors

The following people appear as authors in the inherited history or Cerebellum
Lab development. Counts are from `git shortlog -sne --all` when this file was
updated for the v2.0.0 workflow; counts can increase as development continues.

| Contributor | Historical commits | Affiliation indicated by repository metadata |
| --- | ---: | --- |
| Dan Furie ([djfurie](https://github.com/djfurie)) | 90 | LeafLabs |
| Alexa Jakob | 67 | LeafLabs |
| Jacob Martin ([martinjacobd](https://github.com/martinjacobd)) | 52 | LeafLabs for 51 commits; one local-address commit |
| Griffin Boyle ([griffinboyle-leaflabs](https://github.com/griffinboyle-leaflabs)) | 40 | LeafLabs |
| Kevin DeVries ([kevin-leaflabs-com](https://github.com/kevin-leaflabs-com)) | 30 | LeafLabs |
| Grégory Starck ([gst-toptal](https://github.com/gst-toptal)) | 7 | Toptal email domain in commit history |
| Patrick Edson ([pedson](https://github.com/pedson)) | 7 | No company affiliation stated in commit metadata |
| Benjamin G. Reynolds ([bengreynolds](https://github.com/bengreynolds)) | 4 | Cerebellum Lab / Christie Lab, University of Colorado Anschutz |

Future authors remain credited in Git history and should be added to this table
when the provenance document is updated.

## Organizations and companies represented

- **Mouse-GYM** — owner of the original GitHub repository and upstream project.
- **LeafLabs, LLC** — identified by contributor email domains, the `ll`
  device-tree vendor prefix, and repository build infrastructure references.
- **Toptal** — identified by one contributor's historical commit email domain.
- **Cerebellum Lab / Christie Lab, University of Colorado Anschutz** — owner and
  maintainer of this fork and its reachAQ integration.

Affiliations above document metadata present in the repository; they do not
imply endorsement, copyright ownership, or responsibility for every file.

## Fork-specific development

The first Cerebellum Lab firmware change binds pellet-board `STIM0` to the
complete 5 kHz Tone 1 interval and `STIM1` to the complete 6 kHz Tone 2 interval.
The remaining stimulus outputs are unassigned. Companion reachAQ code labels
these electrical lines for NI-DAQ tone-confirmation correlation.

Pellet firmware release `v2.0.0` establishes the first versioned Cerebellum Lab
behavior release. The maintained workflow uses one controlled build rig to
produce a signed, checksummed release bundle; other rigs deploy that identical
bundle without rebuilding it.

## Licensing status

No top-level license or SPDX license declaration was present in the upstream
repository at the revision listed above, and GitHub reports no detected license.
This fork does not invent or replace the upstream copyright terms. Obtain the
appropriate permission from the relevant rights holders before redistributing
or relicensing material beyond the permissions provided by the hosting service
and applicable law.
