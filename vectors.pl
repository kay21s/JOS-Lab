#!/usr/bin/perl -w

print "hello\n";
for(my $i=0; $i<50; $i++) {
	print "void vector$i();\n";
}
for(my $i=0; $i<50; $i++) {
	print "	SETGATE(idt[$i], 0, GD_KT, vector$i,0)\n";
}
