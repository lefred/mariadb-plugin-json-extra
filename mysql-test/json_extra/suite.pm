package My::Suite::Json_extra;

@ISA = qw(My::Suite);

return "No JSON_EXTRA plugin" unless $ENV{JSON_EXTRA_SO};
return "Not run for embedded server" if $::opt_embedded_server;

push @::global_suppressions,
  (
    qr/Plugin 'json_(diff|patch)' is of maturity level experimental while the server is alpha/,
  );

sub is_default { 1 }

bless { };
