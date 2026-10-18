-- Current Cached.exe alliance-preload ABI with modern donor-backed bodies.
--
-- Current call/bind evidence fixes all three callable signatures and result
-- order.  Classic 3.0, Ertheia and modified protocol-287 exports have the
-- same matching SELECT bodies.  No C1 alliance-war continuation is
-- included because current CAllianceDb::Init does not execute one.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadallallianceid"()
RETURNS TABLE (id integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT alliance.id
    FROM "world"."alliance" AS alliance;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadalliance"(
    p_alliance_id integer
) RETURNS TABLE (
    id integer,
    name varchar(50),
    master_pledge_id integer,
    oust_time integer,
    crest_id integer,
    world_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT alliance.id,
           alliance.name,
           alliance.master_pledge_id,
           alliance.oust_time,
           alliance.crest_id,
           alliance.world_id
    FROM "world"."alliance" AS alliance
    WHERE alliance.id = p_alliance_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getallmemberpledge"(
    p_alliance_id integer
) RETURNS TABLE (pledge_id integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT pledge.pledge_id
    FROM "world"."pledge" AS pledge
    WHERE pledge.alliance_id = p_alliance_id;
$function$;

COMMIT;
