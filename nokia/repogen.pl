#!/usr/bin/perl -w
#
# repogen.pl -- generate shortcut links for the Maemo repository
#

use strict;

sub addsec
{
	my $section = shift;
	print "\t<TR>";
	print "\t\t<TD>$section:</TD>";
	print "\t\t<TD><A href=\"", $_[0]->(), "\">$_</A></TD>" foreach @ARGV;
	print "\t</TR>";
}

# Main starts here
my $mrnc = "https://maemo.research.nokia.com/repository/";
my @comp =  qw(ossw modified nokia-open nokia-closed nuance);

# Construct the path to the product.
die "usage: $0 <product> <queue>..." if @ARGV < 2;
$mrnc .= shift;
$\ = "\n";

# Generate links.html.
print "<HTML><BODY>";
print "<TABLE>";

# Binaries, Sources
for my $bs (qw(binaries source))
{
	print "\t<TR>";
	print "\t\t<TD>", ucfirst($bs), ":</TD>";
	foreach my $queue (@ARGV)
	{
		print "\t\t<TD>";

		# Pull-down menu for w3m.
		print "\t\t\t<NOSCRIPT>";
		print "\t\t\t\t<IMG src=\"$queue\" usemap=\"#$queue-$bs\">";
		print "\t\t\t\t<MAP name=\"$queue-$bs\">";
		print "\t\t\t\t\t<AREA alt=\"all\" href=\"${queue}_binaries.html\">";
		print "\t\t\t\t\t<AREA alt=\"$_\" href=\"$mrnc/pool/default/$queue/$_/$bs\">" foreach @comp;
		print "\t\t\t\t</MAP>";
		print "\t\t\t</NOSCRIPT>";

		# The same in Javascript.  We're writing a program
		# (this script) which writes a program (print) which
		# writes a program (document.write).
		print "\t\t\t<SCRIPT type=\"text/javascript\">";
		print "\t\t\t\tdocument.write(\"<SELECT>\");";
		print "\t\t\t\t\tdocument.write(\"<OPTION onclick=\\\"document.location='${queue}_binaries.html'\\\">$queue</OPTION>\");";
		print "\t\t\t\t\tdocument.write(\"<OPTION onclick=\\\"document.location='$mrnc/pool/default/$queue/$_/$bs'\\\">$_</OPTION>\");" foreach @comp;
		print "\t\t\t\tdocument.write(\"</SELECT>\");";
		print "\t\t\t</SCRIPT>";

		print "\t\t</TD>";
	}
	print "\t</TR>";
}

addsec('Dists',		sub { "$mrnc/dists/default/$_"	});
addsec('Releases',	sub { "$mrnc/releases/$_"	});
addsec('Images',	sub { "$mrnc/releases/$_/flash"	});

print "</TABLE>";
print "</BODY></HTML>";

# Generate the combined package pages.
foreach my $bs (qw(binaries sources))
{
	foreach my $queue (@ARGV)
	{
		open(FH, '>', "${queue}_$bs.html");
		print FH "<HTML>";
		print FH "\t<FRAMESET rows=\"10%, 10%, 10%, 10%, 10%\">";
		print FH "\t\t<FRAME src=\"links.html\">";
		print FH "\t\t<FRAME src=\"$mrnc/pool/default/$queue/$_/$bs\">"
			foreach @comp;
		print FH "\t</FRAMESET>";
		print FH "</HTML>";
		close(FH);
	}
}

# End of gen.pl
