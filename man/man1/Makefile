PAGES=blight.1.pdf busylight.1.pdf rbserver.1.pdf setsn.1.pdf mutemond.1.pdf calmond.1.pdf

all: $(PAGES)

blight.1.pdf: blight.1
	groff -man $< | ps2pdf - $@

busylight.1.pdf: busylight.1
	groff -man $< | ps2pdf - $@

rbserver.1.pdf: rbserver.1
	tbl $< | groff -man | ps2pdf - $@

setsn.1.pdf: setsn.1
	groff -man $< | ps2pdf - $@

calmond.1.pdf: calmond.1
	groff -man $< | ps2pdf - $@

mutemond.1.pdf: mutemond.1
	groff -man $< | ps2pdf - $@
