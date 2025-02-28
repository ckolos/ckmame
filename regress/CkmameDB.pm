package CkmameDB;

use strict;
use warnings;

my %status = (baddump => 1, nodump => 2);

sub new {
	my $class = UNIVERSAL::isa ($_[0], __PACKAGE__) ? shift : __PACKAGE__;
	my $self = bless {}, $class;

	my ($dir, $skip, $unzipped, $no_hashes, $detector_hashes, $not_in_db, $verbose) = @_;
	
	$self->{dir} = $dir;
	$self->{unzipped} = $unzipped;
	$self->{no_hashes} = {};
	$self->{detector_hashes} = {};
	$self->{detectors} = {};
	$self->{verbose} = $verbose;

	if (defined($no_hashes)) {
		for my $no_hash (@$no_hashes) {
			my ($cache_dir, $archive, $file, $hashes) = @$no_hash;

			next unless ($cache_dir eq $dir);

			if (defined($file)) {
				$self->{no_hashes}->{$archive}->{$file} = $hashes // 1;
			}
			else {
				$self->{no_hashes}->{$archive} = 1;
			}
		}
	}
	
	if (defined($detector_hashes)) {
		for my $detector_hash (@{$detector_hashes}) {
			my ($name, $version, $cache_dir, $archive, $file) = @$detector_hash;
			
			next unless ($cache_dir eq $dir);
			
			my $detector_key = "$name|$version";
			
			$self->{detectors}->{$detector_key} = 1;

			$file //= "";
			$self->{detector_hashes}->{$archive}->{$file}->{$detector_key} = 1;
		}
	}
	
	if ($skip) {
		$self->{skip} = { map { $_ => 1} @$skip };
	}
	else {
		$self->{skip} = {};
	}
	
	if (defined($not_in_db)) {
		for my $entry (@$not_in_db) {
			my ($cache_dir, $archive) = @$entry;
			
			next unless ($cache_dir eq $dir);
			
			$self->{skip}->{$archive} = 1;
		}
	}

	$self->{dump_got} = [];
	$self->{dump_archives} = {};
	$self->{max_id} = 0;
	$self->{archives_got} = {};
	
	return $self;
}

sub read_db {
	my ($self) = @_;
	
	if (! -f "$self->{dir}/.ckmame.db") {
		$self->{dump_got} = [
			'>>> table archive (archive_id, name, mtime, size, file_type)',
			'>>> table detector (detector_id, name, version)',
			'>>> table file (archive_id, file_idx, name, mtime, status, size, crc, md5, sha1, detector_id)'
		];
		return 1;
	}
	
	my $dump;
	unless (open $dump, "../dbdump $self->{dir}/.ckmame.db |") {
		print "dbdump in $self->{dir}/.ckmame.db failed: $!\n" if ($self->{verbose});
		return undef;
	}

	$self->{dump_detectors} = {};
	my %archive_names;
	my @files = ();

	my $table = '';
	while (my $line = <$dump>) {
		chomp $line;
		
		if ($table eq 'file') {
			my ($id, $idx, $name, $mtime, $status, $size, $crc, $md5, $sha1, $detector_id) = split '\|', $line;
			push @files, [ $id, $idx, $detector_id, $line ];
			if (exists($archive_names{$id})) {
				my $dump_archive = $self->{dump_archives}->{$archive_names{$id}};

				if ($name ne "") {
					$dump_archive->{files}->{$name} = $idx;
				}
				if ($detector_id > 0) {
					$dump_archive->{detector_hashes}->{$idx}->{$detector_id} = {
						size => $size,
						crc => $crc,
						md5 => $md5,
						sha1 => $sha1
					};
				}
				if ($idx > $dump_archive->{max_idx}) {
					$dump_archive->{max_idx} = $idx;
				}
			}
			next;
		}
		push @{$self->{dump_got}}, $line;
		if ($line =~ m/>>> table (\w+)/) {
			$table = $1;
			next;
		}
		if ($table eq 'archive') {
			my ($id, $name) = split '\|', $line;
			$archive_names{$id} = $name;
			$self->{dump_archives}->{$name} = { id => $id, files => {}, max_idx => 0 };
			if ($id > $self->{max_id}) {
				$self->{max_id} = $id;
			}
		}
		elsif ($table eq 'detector') {
			my ($id, $name, $version) = split '\|', $line;
			$self->{dump_detectors}->{"$name|$version"} = $id;
		}
	}
	close($dump);

	for my $file (sort {
		if ($a->[0] == $b->[0]) {
			if ($a->[1] == $b->[1]) {
				return $a->[2] <=> $b->[2];
			}
			else {
				return $a->[1] <=> $b->[1];
			}
		}
		else {
			return $a->[0] <=> $b->[0];
		}
	} @files) {
		push @{$self->{dump_got}}, $file->[3];
	}
	
	return 1;
}


sub read_archives {
	my ($self) = @_;
	
	my $dat;
	my $opt = ($self->{unzipped} ? '--roms-unzipped' : '');
	unless (open $dat, "../../src/mkmamedb --runtest $opt -o /dev/stdout $self->{dir} 2>/dev/null | ") {
		print "mkmamedb using $self->{dir} failed: $!\n" if ($self->{verbose});
		return undef;
	}

	my $archive;
	my $prefix;
	my $idx;
	while (my $line = <$dat>) {
		chomp $line;

		next if ($line =~ m/^#/);

		unless ($line =~ m/^(\S+) (.*)/) {
			print "can't parse mtree line '$line'\n" if ($self->{verbose});
			return undef;
		}

		my $name = $1;
		my @args = split ' ', $2;

		next if ($name eq '.');

		$name =~ s,^\./,,;
		$name = destrsvis($name);
		my %attributes = ();
		for my $attr (@args) {
			unless ($attr =~ m/^([^=]+)=(.*)/) {
				print "can't parse mtree line '$line'\n" if ($self->{verbose});
				return undef;
			}
			$attributes{$1} = $2;
		}


		if ($attributes{type} eq 'dir') {
			if ($self->{skip}->{$name}) {
				undef $archive;
				next;
			}

			$prefix = $name;
			$archive = { name => $name eq "" ? "." : $name, files => [], file_type => 0 };
			$idx = 0;

			my @stat = stat("$self->{dir}/$archive->{name}");
			$archive->{mtime} = ($archive->{name} eq "." ? 0 : ($stat[9] // '<null>'));
			if (-f "$self->{dir}/$archive->{name}") {
				$archive->{size} = $stat[7] // '<null>';
			}
			else {
				$archive->{size} = 0;
			}
			if ($self->{dump_archives}->{$archive->{name}}) {
				$archive->{id} = $self->{dump_archives}->{$archive->{name}}->{id};
			}
			else {
				$archive->{id} = ++$self->{max_id};
			}
			$self->{archives_got}->{$archive->{id}} = $archive;
		}
		elsif ($attributes{type} eq 'file') {
			next unless ($archive);

			$name =~ s,^$prefix/,,;

			my $rom = { name => $name, idx => $idx++, status => 0 };
			$rom->{size} = $attributes{size} // '-1';
			for my $attr (qw(sha1 md5)) {
				$rom->{$attr} = $attributes{$attr} // 'null';
			}
			if (exists($attributes{status})) {
				if (!exists($status{$attributes{status}})) {
					print "unknown file status '$attributes{status}'\n" if ($self->{verbose});
					return undef;
				}
				$rom->{status} = $status{$attributes{status}};
			}
			$rom->{mtime} = $attributes{time};
			if (exists($attributes{crc})) {
				$rom->{crc} = hex($attributes{crc});
			}
			else {
				$rom->{crc} = "<null>";
				$archive->{file_type} = 1;
			}
			
			$self->omit_hashes($archive->{name}, $rom);

			push @{$archive->{files}}, $rom;
		}
	}

	close($dat);
	
	return 1;
}

sub make_dump {
	my ($self) = @_;
	
	my @dump = ();
	
	push @dump, '>>> table archive (archive_id, name, mtime, size, file_type)';
	
	for my $id (sort { $a <=> $b } keys %{$self->{archives_got}}) {
		my $archive = $self->{archives_got}->{$id};
		push @dump, "$id|$archive->{name}|$archive->{mtime}|$archive->{size}|$archive->{file_type}";
	}
	
	push @dump, '>>> table detector (detector_id, name, version)';
	
	my $next_id = 1;
	
	for my $id (values %{$self->{dump_detectors}}) {
		if ($id >= $next_id) {
			$next_id = $id + 1;
		}
	}
	
	my %detectors;
	my %detector_ids;
	
	for my $detector (keys %{$self->{detectors}}) {
		if (exists($self->{dump_detectors}->{$detector})) {
			$detectors{$self->{dump_detectors}->{$detector}} = $detector;
			$detector_ids{$detector} = $self->{dump_detectors}->{$detector};
		}
		else {
			$detectors{$next_id} = $detector;
			$detector_ids{$detector} = $next_id;
 			$next_id += 1;
		}
	}
	
	for my $id (sort keys %detectors) {
		push @dump, "$id|$detectors{$id}";
	}
	
	push @dump, '>>> table file (archive_id, file_idx, name, mtime, status, size, crc, md5, sha1, detector_id)';

	for my $id (sort { $a <=> $b} keys %{$self->{archives_got}}) {
		my $archive = $self->{archives_got}->{$id};
		my $dump_archive = $self->{dump_archives}->{$archive->{name}};

		if ($self->{unzipped}) {
			my $next_idx = 0;
			if (defined($dump_archive)) {
				$next_idx = $dump_archive->{max_idx} + 1;
			}
			for my $file (@{$archive->{files}}) {
				if ($dump_archive && exists($dump_archive->{files}->{$file->{name}})) {
					$file->{idx} = $dump_archive->{files}->{$file->{name}};
				}
				else {
					$file->{idx} = $next_idx++;
				}
			}

			$archive->{files} = [ sort { $a->{idx} <=> $b->{idx} } @{$archive->{files}} ];
		}
		for my $file (@{$archive->{files}}) {
			push @dump, join '|', $id, $file->{idx}, $file->{name}, $file->{mtime}, $file->{status}, $file->{size}, $file->{crc}, "<$file->{md5}>", "<$file->{sha1}>", 0;
			
			my $detector_hashes = $self->{detector_hashes}->{$archive->{name}};
			if ($detector_hashes) {
				my $detector_names = $detector_hashes->{$file->{name}} // $detector_hashes->{""};
				my @ids = sort map { $detector_ids{$_}; } keys %$detector_names;
				for my $detector_id (@ids) {
					my $dump_file;
					if (defined($dump_archive)) {
						$dump_file = $dump_archive->{detector_hashes}->{$file->{idx}}->{$detector_id};
					}
					if (defined($dump_file)) {
						push @dump, join '|', $id, $file->{idx}, "", 0, 0, $dump_file->{size}, $dump_file->{crc}, "$dump_file->{md5}", "$dump_file->{sha1}", $detector_id;
					}
					else {
						push @dump, join '|', $id, $file->{idx}, "", 0, 0, "?", "?", "<?>", "<?>", $detector_id;
					}
				}
			}
		}
	}
	
	$self->{dump_expected} = \@dump;
	
	return 1;
}


sub omit_hashes {
	my ($self, $archive, $file) = @_;

	my $omit;

	return unless (exists($self->{no_hashes}->{$archive}));
	if (ref($self->{no_hashes}->{$archive}) eq 'HASH') {
		$omit = $self->{no_hashes}->{$archive}->{$file->{name}};
	}
	else {
		$omit = 1;
	}

	return unless defined($omit);

	if ($omit eq '1') {
		$omit = 'md5,sha1';
	}

	$file->{status} = 0;
	for my $hash (split ',', $omit) {
		$file->{$hash} = 'null';
	}
}


sub destrsvis {
	my ($str) = @_;

	$str =~ s/\\a/\a/g;
	$str =~ s/\\b/\b/g;
	$str =~ s/\\f/\f/g;
	$str =~ s/\\n/\n/g;
	$str =~ s/\\r/\r/g;
	$str =~ s/\\s/ /g;
	$str =~ s/\\t/\t/g;
	$str =~ s/\\v/\cK/g;
	$str =~ s/\\(#|\\)/$1/g;

	return $str;
}
1;
