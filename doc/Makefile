# Minimal makefile for Sphinx documentation
#

ifeq ($(VERBOSE),1)
  Q =
else
  Q = @
endif

# You can set these variables from the command line.
SPHINXOPTS    ?= -q
SPHINXBUILD   = sphinx-build
SPHINXPROJ    = "Project ACRN"
BUILDDIR      ?= _build
SOURCEDIR     = $(BUILDDIR)/rst
LATEXMKOPTS   = "-silent -f"

# should the config option doc show hidden config options?
XSLTPARAM     ?= --stringparam showHidden 'n'

# document publication assumes the folder structure is setup
# with the acrn-hypervisor and projectacrn.github.io repos as
# sibling folders and make is run inside the acrn-hypervisor/docs
# folder.

ACRN_BASE     = "$(CURDIR)/../.."
DOC_TAG      ?= development
RELEASE      ?= latest
PUBLISHDIR    = $(ACRN_BASE)/projectacrn.github.io/$(RELEASE)

# Put it first so that "make" without argument is like "make help".
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(OPTS)
	@echo ""
	@echo "make publish"
	@echo "   publish generated html to projectacrn.github.io site:"
	@echo "   specify RELEASE=name to publish as a tagged release version"
	@echo "   and placed in a version subfolder.  Requires repo merge permission."

.PHONY: help Makefile copy-to-sourcedir doxy content html singlehtml clean publish

# Generate the doxygen xml (for Sphinx) and copy the doxygen html to the
# api folder for publishing along with the Sphinx-generated API docs.
# This is where we tweak the "pre" API comments into "preconditions"
# Note that this step starts with an empty doc.log while the others append
doxy:
	$(Q)(cat $(SOURCEDIR)/acrn.doxyfile ; echo "OUTPUT_DIRECTORY=$(SOURCEDIR)/doxygen" ) | doxygen - > $(BUILDDIR)/doc.log 2>&1

# Copy all the rst content (and images, etc) into the _build/rst folder
# including rst content and xsd files from the /misc folder that we'll
# use to generate config option documentation

dscontent:
	@echo making rst content for pure Design Spec from code
	$(Q)mkdir -p $(SOURCEDIR)
	$(Q)rsync -rt --exclude=$(BUILDDIR) --exclude-from=etc/mds_only_exclude_list.txt . $(SOURCEDIR)
	$(Q)mv $(SOURCEDIR)/etc/mds_only_index.rst.txt $(SOURCEDIR)/index.rst

sascontent: dscontent
	@echo preparing rst content for SAS
	$(Q)cat $(SOURCEDIR)/etc/sas_exclude_symbols.yml >> $(SOURCEDIR)/acrn.doxyfile
	$(Q)mv $(SOURCEDIR)/etc/sas.rst.txt $(SOURCEDIR)/developer-guides/mds/index.rst

content:
	@echo making rst content for all documentation
	$(Q)mkdir -p $(SOURCEDIR)
	$(Q)rsync -rt --exclude=$(BUILDDIR) . $(SOURCEDIR)
	$(Q)scripts/extract_content.py $(SOURCEDIR) misc
	$(Q)mkdir -p $(SOURCEDIR)/misc/config_tools/schema
	$(Q)rsync -rt ../misc/config_tools/schema/*.xsd $(SOURCEDIR)/misc/config_tools/schema
	$(Q)xsltproc $(XSLTPARAM) -xinclude ./scripts/configdoc.xsl $(SOURCEDIR)/misc/config_tools/schema/config.xsd > $(SOURCEDIR)/reference/configdoc.txt

gen_html: doxy
	@echo making HTML content
	$(Q)./scripts/show-versions.py
	-$(Q)$(SPHINXBUILD) -t $(DOC_TAG) -b html -d $(BUILDDIR)/doctrees $(SOURCEDIR) $(BUILDDIR)/html $(SPHINXOPTS) $(OPTS) >> $(BUILDDIR)/doc.log 2>&1
	$(Q)./scripts/filter-doc-log.sh $(BUILDDIR)/doc.log

mdshtml: dscontent
	$(MAKE) gen_html
	@echo MDS HTML generated

sashtml: sascontent
	$(MAKE) gen_html
	@echo SAS HTML generated

html: content
	$(MAKE) gen_html
	@echo Full documentation HTML generated

gen_singlehtml: doxy
	@echo making single HTML content
	$(Q)./scripts/show-versions.py
	$(Q)$(SPHINXBUILD) -t $(DOC_TAG) -b singlehtml -d $(BUILDDIR)/doctrees $(SOURCEDIR) $(BUILDDIR)/html $(SPHINXOPTS) $(OPTS) >> $(BUILDDIR)/doc.log 2>&1
	$(Q)./scripts/filter-doc-log.sh $(BUILDDIR)/doc.log

mds_singlehtml: dscontent
	$(MAKE) gen_singlehtml
	@echo MDS single HTML generated

sas_singlehtml: sascontent
	$(MAKE) gen_singlehtml
	@echo SAS single HTML generated

singlehtml: content
	$(MAKE) gen_singlehtml
	@echo Full documentation single HTML generated

DS_PDF  = $(BUILDDIR)/design_spec.pdf
MDS_PDF = $(BUILDDIR)/acrn_mds.pdf
SAS_PDF = $(BUILDDIR)/acrn_sas.pdf

dspdf:
	@echo making $(DS_PDF)
	$(Q)wkhtmltopdf --enable-local-file-access --disable-javascript $(BUILDDIR)/html/index.html $(DS_PDF)

mdspdf: mds_singlehtml
	$(MAKE) dspdf
	@echo making $(MDS_PDF)
	$(Q)mv $(DS_PDF) $(MDS_PDF)

saspdf: sas_singlehtml
	$(MAKE) dspdf
	@echo making $(SAS_PDF)
	$(Q)mv $(DS_PDF) $(SAS_PDF)

pdf: html
	@echo now making $(BUILDDIR)/latex/acrn.pdf
	$(Q)make -silent latexpdf LATEXMKOPTS=$(LATEXMKOPTS) >> $(BUILDDIR)/doc.log 2>&1
	$(Q)./scripts/filter-doc-log.sh $(BUILDDIR)/doc.log


# Remove generated content (Sphinx and doxygen)

clean:
	rm -fr $(BUILDDIR)


# Copy material over to the GitHub pages staging repo
# along with a README, index.html redirect to latest/index.html, robots.txt (for
# search exclusions), and tweak the Sphinx-generated 404.html to work as the
# site-wide 404 response page.  (We generate the 404.html with Sphinx so it has
# the current left navigation contents and overall style.)

publish:
	mkdir -p $(PUBLISHDIR)
	cd $(PUBLISHDIR)/..; git pull origin master
	rm -fr $(PUBLISHDIR)/*
	cp -r $(BUILDDIR)/html/* $(PUBLISHDIR)
ifeq ($(RELEASE),latest)
	cp scripts/publish-README.md $(PUBLISHDIR)/../README.md
	scripts/publish-redirect.sh $(PUBLISHDIR)/../index.html latest/index.html
	scripts/publish-redirect.sh $(PUBLISHDIR)/../hardware.html latest/reference/hardware.html
	cp scripts/publish-robots.txt $(PUBLISHDIR)/../robots.txt
	sed 's/<head>/<head>\n  <base href="https:\/\/projectacrn.github.io\/latest\/">/' $(BUILDDIR)/html/404.html > $(PUBLISHDIR)/../404.html
endif
	cd $(PUBLISHDIR)/..; git add -A; git commit -s -m "publish $(RELEASE)"; git push origin master;


# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(OPTS) is meant as a shortcut for $(SPHINXOPTS).
%: Makefile
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(OPTS)
