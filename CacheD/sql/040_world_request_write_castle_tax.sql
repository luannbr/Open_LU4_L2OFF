-- Current Cached.exe primary opcode 28 (RequestWriteCastleTax) persistence.
--
-- RequestWriteCastleTax 0x0048d780 and its five-int DB helper fix the exact
-- callable ABI. The relational body is identical in the Classic 3.0,
-- Ertheia and modified-287 donor families. The unmatched discriminator case
-- remains a successful no-op, as in the SQL Server body.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_writecastletax"(
    p_is_castle integer,
    p_tax_type integer,
    p_tax_rate integer,
    p_to_change integer,
    p_residence_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_is_castle = 1 THEN
        IF p_tax_type = 2 THEN
            UPDATE "world"."castle"
            SET "tax_child_rate" = p_tax_rate,
                "tax_child_rate_to_change" = p_to_change
            WHERE "id" = p_residence_id;
        ELSIF p_tax_type = 1 THEN
            UPDATE "world"."castle"
            SET "tax_rate" = p_tax_rate,
                "tax_rate_to_change" = p_to_change
            WHERE "id" = p_residence_id;
        ELSE
            RAISE EXCEPTION
                'tax type is invalid. castle cannot save type[%] id and castle id[%].',
                p_tax_type,
                p_residence_id;
        END IF;
    ELSIF p_is_castle = 0 THEN
        IF p_tax_type = 2 THEN
            UPDATE "world"."agit"
            SET "tax_child_rate" = p_tax_rate,
                "tax_child_rate_to_change" = p_to_change
            WHERE "id" = p_residence_id;
        ELSIF p_tax_type = 1 THEN
            UPDATE "world"."agit"
            SET "tax_rate" = p_tax_rate,
                "tax_rate_to_change" = p_to_change
            WHERE "id" = p_residence_id;
        ELSE
            RAISE EXCEPTION
                'tax type is invalid. agit cannot save type[%] id and agit id[%].',
                p_tax_type,
                p_residence_id;
        END IF;
    END IF;
END;
$function$;

COMMIT;
