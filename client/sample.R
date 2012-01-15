#!/usr/bin/Rscript

library(tcltk)
x11()

args <- commandArgs(TRUE)

if (length(args) < 4) {
    prompt  <- "Not enough command line parameters!"
    extra   <- "Usage: Rscript plot.R TABLE NUM_COLS FROM TO CNT"
    capture <- tk_messageBox(message = prompt, detail = extra)
    quit(save='NO', status=1)
}

tab_fname <- args[1]
cols <- as.integer(args[2])
from <- as.integer(args[3])
to <- as.integer(args[4])
count <- as.integer(args[5])

t <- read.table(tab_fname, header=F)

if(cols > 0) {
    plot(t$V2[from:to][1:count %% count == 0], type='b', col='green')
}
if(cols > 1) {
    lines(t$V3[from:to][1:count %% count == 0], type='b', col='magenta')
}
if(cols > 2) {
    lines(t$V4[from:to][1:count %% count == 0], type='b', col='yellow')
}
if(cols > 3) {
    lines(t$V5[from:to][1:count %% count == 0], type='b', col=386)
}
if(cols > 4) {
    lines(t$V6[from:to][1:count %% count == 0], type='b', col='blue')
}
if(cols > 5) {
    lines(t$V7[from:to][1:count %% count == 0], type='b', col='red')
}
if(cols > 6) {
    lines(t$V8[from:to][1:count %% count == 0], type='b', col='black')
}
if(cols > 7) {
    lines(t$V9[from:to][1:count %% count == 0], type='b', col='gray')
}

prompt  <- "hit spacebar to close plots"
extra   <- ""
capture <- tk_messageBox(message = prompt, detail = extra)
