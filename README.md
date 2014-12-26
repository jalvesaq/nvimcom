# nvimcom

This is the development version of the R package *nvimcom*, which runs a
server in R to receive messages from the Neovim plugin [Nvim-R].

## How to install

The easiest way to install nvimcom is to use the [devtools] package.

```s
devtools::install_github("nvimcom", "jalvesaq")
```

To manually download and install nvimcom, do the following in a terminal
emulator:

```sh
git clone https://github.com/jalvesaq/nvimcom.git
R CMD INSTALL nvimcom
```

[Nvim-R]: https://github.com/jalvesaq/Nvim-R
[Neovim]: http://neovim.org
[devtools]: http://cran.r-project.org/web/packages/devtools/index.html
